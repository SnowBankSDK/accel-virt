import io
import json
import pathlib
import sys
import tempfile
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "mcp"))
import mcp_server  # noqa: E402


class Echo:
    def tools(self):
        return [mcp_server.tool("echo", "echo", {"text": {"type": "string"}}, ["text"]),
                mcp_server.tool("refuse", "refuse")]

    def echo(self, text):
        return {"text": text}

    def refuse(self):
        raise mcp_server.ToolError("refused")


def call(server, name, arguments):
    request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": name, "arguments": arguments}}
    return server.handle(json.dumps(request))


class ProtocolTest(unittest.TestCase):
    def setUp(self):
        self.server = mcp_server.Server(Echo(), {"name": "test", "version": "1"}, io.StringIO(), io.StringIO())

    def test_tool_result_is_text_content(self):
        answer = call(self.server, "echo", {"text": "hi"})
        self.assertFalse(answer["result"]["isError"])
        self.assertEqual(json.loads(answer["result"]["content"][0]["text"]), {"text": "hi"})

    def test_tool_error_is_an_error_result_not_a_protocol_error(self):
        answer = call(self.server, "refuse", {})
        self.assertTrue(answer["result"]["isError"])
        self.assertEqual(answer["result"]["content"][0]["text"], "refused")

    def test_unknown_tool_and_bad_arguments_are_protocol_errors(self):
        self.assertEqual(call(self.server, "nothing", {})["error"]["code"], -32602)
        self.assertEqual(call(self.server, "echo", {})["error"]["code"], -32602)
        self.assertEqual(call(self.server, "echo", {"text": "a", "extra": 1})["error"]["code"], -32602)

    def test_notifications_get_no_reply(self):
        self.assertIsNone(self.server.handle(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"})))


class JobsTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.jobs = mcp_server.Jobs(self.scratch.name)

    def tearDown(self):
        for job in self.jobs.list():
            if job["state"] == "running":
                self.jobs.cancel(job["id"])
        self.scratch.cleanup()

    def test_exit_code_and_output_are_kept(self):
        good = self.jobs.start("good", ["sh", "-c", "echo first; echo second"], self.scratch.name)
        bad = self.jobs.start("bad", ["sh", "-c", "echo oops >&2; exit 3"], self.scratch.name)
        self.assertEqual(self.jobs.wait(good["id"], 10)["state"], "succeeded")
        done = self.jobs.wait(bad["id"], 10)
        self.assertEqual((done["state"], done["exit_code"]), ("failed", 3))
        self.assertIn("oops", done["tail"])
        self.assertEqual(self.jobs.log(good["id"], grep="sec"), "second")

    def test_state_survives_a_new_server(self):
        job = self.jobs.start("slow", ["sleep", "30"], self.scratch.name)
        again = mcp_server.Jobs(self.scratch.name)
        self.assertEqual(again.status(job["id"])["state"], "running")
        self.assertEqual(again.cancel(job["id"])["state"], "cancelled")

    def test_cancel_stops_the_children_too(self):
        marker = pathlib.Path(self.scratch.name) / "child.pid"
        job = self.jobs.start("tree", ["sh", "-c", f"sleep 60 & echo $! > {marker}; wait"], self.scratch.name)
        deadline = time.time() + 5
        while not marker.exists() and time.time() < deadline:
            time.sleep(0.05)
        child = int(marker.read_text())
        self.jobs.cancel(job["id"])
        deadline = time.time() + 5
        while self.jobs.alive(child) and time.time() < deadline:
            time.sleep(0.05)
        self.assertFalse(self.jobs.alive(child))

    def test_exclusive_work_is_refused_while_it_runs(self):
        self.jobs.start("first", ["sleep", "30"], self.scratch.name, exclusive="vm:local")
        with self.assertRaisesRegex(mcp_server.ToolError, "already running vm:local"):
            self.jobs.start("second", ["true"], self.scratch.name, exclusive="vm:local")
        self.jobs.start("other", ["true"], self.scratch.name, exclusive="vm:remote")

    def test_wait_is_bounded(self):
        job = self.jobs.start("slow", ["sleep", "30"], self.scratch.name)
        started = time.time()
        self.assertEqual(self.jobs.wait(job["id"], 1)["state"], "running")
        self.assertLess(time.time() - started, 5)

    def test_unknown_job_is_a_tool_error(self):
        with self.assertRaises(mcp_server.ToolError):
            self.jobs.status("0000-000000-none")


class BuildLimitsTest(unittest.TestCase):
    def test_builds_take_the_lock_and_the_limits_the_environment_sets(self):
        limits = mcp_server.BuildLimits(lock="/run/test.lock", environment={
            "LSW_BUILD_JOBS": "3", "LSW_BUILD_CPU_QUOTA": "300%", "LSW_BUILD_MEMORY_HIGH": "5G"})
        wrapped = limits.wrap(["cmake", "--build", "build"])
        self.assertEqual(wrapped[:2], ["flock", "/run/test.lock"])
        self.assertIn("CPUQuota=300%", wrapped)
        self.assertIn("MemoryHigh=5G", wrapped)
        self.assertEqual(wrapped[-3:], ["cmake", "--build", "build"])
        self.assertEqual(limits.environment()["CMAKE_BUILD_PARALLEL_LEVEL"], "3")

    def test_without_settings_builds_use_every_cpu_and_no_quota(self):
        limits = mcp_server.BuildLimits(lock="/run/test.lock", environment={})
        wrapped = limits.wrap(["true"])
        self.assertFalse(any(part.startswith(("CPUQuota", "MemoryHigh")) for part in wrapped))
        self.assertEqual(limits.environment()["CMAKE_BUILD_PARALLEL_LEVEL"], str(mcp_server.os.cpu_count()))


if __name__ == "__main__":
    unittest.main()
