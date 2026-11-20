// THIS FILE IS GENERATED. DO NOT EDIT.
// Regenerate with: python -m av_codegen.opencl --xml cl.xml --out ...

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 300
#endif
#define CL_USE_DEPRECATED_OPENCL_1_0_APIS
#define CL_USE_DEPRECATED_OPENCL_1_1_APIS
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_USE_DEPRECATED_OPENCL_2_0_APIS
#define CL_USE_DEPRECATED_OPENCL_2_1_APIS
#define CL_USE_DEPRECATED_OPENCL_2_2_APIS

#include <CL/cl.h>
#include <CL/cl_gl.h>

#include "proto/client/client_transport.hpp"

namespace av::clproto {

template <typename Word>
struct GuestWord {
    using type = Word;
};

template <typename Word>
using guest_size_t = typename GuestWord<Word>::type;

template <typename Native, typename Mirror, bool SameWidth>
struct GuestLayout {
    using type = Mirror;
};

template <typename Native, typename Mirror>
struct GuestLayout<Native, Mirror, true> {
    using type = Native;
};

template <typename Word>
struct cl_buffer_region_words {
    Word origin;
    Word size;
};

template <typename Word>
using guest_cl_buffer_region = typename GuestLayout<cl_buffer_region, cl_buffer_region_words<Word>,
                                         sizeof(Word) == sizeof(size_t)>::type;

template <typename Word>
struct cl_image_desc_words {
    cl_mem_object_type image_type;
    Word image_width;
    Word image_height;
    Word image_depth;
    Word image_array_size;
    Word image_row_pitch;
    Word image_slice_pitch;
    cl_uint num_mip_levels;
    cl_uint num_samples;
    Word buffer;
};

template <typename Word>
using guest_cl_image_desc = typename GuestLayout<cl_image_desc, cl_image_desc_words<Word>,
                                      sizeof(Word) == sizeof(size_t)>::type;

template <typename Word = size_t>
cl_int av_clGetPlatformIDs(::av::ClientTransport& t, cl_uint num_entries, uint64_t* platforms, cl_uint* num_platforms);
template <typename Word = size_t>
cl_int av_clGetPlatformInfo(::av::ClientTransport& t, uint64_t platform, cl_platform_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clGetDeviceIDs(::av::ClientTransport& t, uint64_t platform, cl_device_type device_type, cl_uint num_entries, uint64_t* devices, cl_uint* num_devices);
template <typename Word = size_t>
cl_int av_clGetDeviceInfo(::av::ClientTransport& t, uint64_t device, cl_device_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
uint64_t av_clCreateContext(::av::ClientTransport& t, const uint64_t* properties, cl_uint num_devices, const uint64_t* devices, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateContextFromType(::av::ClientTransport& t, const uint64_t* properties, cl_device_type device_type, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clRetainContext(::av::ClientTransport& t, uint64_t context);
template <typename Word = size_t>
cl_int av_clReleaseContext(::av::ClientTransport& t, uint64_t context);
template <typename Word = size_t>
cl_int av_clGetContextInfo(::av::ClientTransport& t, uint64_t context, cl_context_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
uint64_t av_clCreateCommandQueue(::av::ClientTransport& t, uint64_t context, uint64_t device, cl_command_queue_properties properties, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateCommandQueueWithProperties(::av::ClientTransport& t, uint64_t context, uint64_t device, const uint64_t* properties, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clRetainCommandQueue(::av::ClientTransport& t, uint64_t command_queue);
template <typename Word = size_t>
cl_int av_clReleaseCommandQueue(::av::ClientTransport& t, uint64_t command_queue);
template <typename Word = size_t>
cl_int av_clGetCommandQueueInfo(::av::ClientTransport& t, uint64_t command_queue, cl_command_queue_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clFlush(::av::ClientTransport& t, uint64_t command_queue);
template <typename Word = size_t>
cl_int av_clFinish(::av::ClientTransport& t, uint64_t command_queue);
template <typename Word = size_t>
uint64_t av_clCreateBuffer(::av::ClientTransport& t, uint64_t context, cl_mem_flags flags, guest_size_t<Word> size, void* host_ptr, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateSubBuffer(::av::ClientTransport& t, uint64_t buffer, cl_mem_flags flags, cl_buffer_create_type buffer_create_type, const guest_cl_buffer_region<Word>* buffer_create_info, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateImage(::av::ClientTransport& t, uint64_t context, cl_mem_flags flags, const cl_image_format* image_format, const guest_cl_image_desc<Word>* image_desc, uint64_t image_desc_buffer, void* host_ptr, guest_size_t<Word> host_ptr_bytes, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clRetainMemObject(::av::ClientTransport& t, uint64_t memobj);
template <typename Word = size_t>
cl_int av_clReleaseMemObject(::av::ClientTransport& t, uint64_t memobj);
template <typename Word = size_t>
cl_int av_clGetMemObjectInfo(::av::ClientTransport& t, uint64_t memobj, cl_mem_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clGetImageInfo(::av::ClientTransport& t, uint64_t image, cl_image_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clGetSupportedImageFormats(::av::ClientTransport& t, uint64_t context, cl_mem_flags flags, cl_mem_object_type image_type, cl_uint num_entries, cl_image_format* image_formats, guest_size_t<Word> image_formats_bytes, cl_uint* num_image_formats);
template <typename Word = size_t>
cl_int av_clEnqueueReadBuffer(::av::ClientTransport& t, uint64_t command_queue, uint64_t buffer, cl_bool blocking_read, guest_size_t<Word> offset, guest_size_t<Word> size, void* ptr, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueWriteBuffer(::av::ClientTransport& t, uint64_t command_queue, uint64_t buffer, cl_bool blocking_write, guest_size_t<Word> offset, guest_size_t<Word> size, const void* ptr, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueCopyBuffer(::av::ClientTransport& t, uint64_t command_queue, uint64_t src_buffer, uint64_t dst_buffer, guest_size_t<Word> src_offset, guest_size_t<Word> dst_offset, guest_size_t<Word> size, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueFillBuffer(::av::ClientTransport& t, uint64_t command_queue, uint64_t buffer, const void* pattern, guest_size_t<Word> pattern_size, guest_size_t<Word> offset, guest_size_t<Word> size, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueReadImage(::av::ClientTransport& t, uint64_t command_queue, uint64_t image, cl_bool blocking_read, const guest_size_t<Word>* origin, const guest_size_t<Word>* region, guest_size_t<Word> row_pitch, guest_size_t<Word> slice_pitch, void* ptr, guest_size_t<Word> ptr_bytes, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueWriteImage(::av::ClientTransport& t, uint64_t command_queue, uint64_t image, cl_bool blocking_write, const guest_size_t<Word>* origin, const guest_size_t<Word>* region, guest_size_t<Word> input_row_pitch, guest_size_t<Word> input_slice_pitch, const void* ptr, guest_size_t<Word> ptr_bytes, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueCopyImage(::av::ClientTransport& t, uint64_t command_queue, uint64_t src_image, uint64_t dst_image, const guest_size_t<Word>* src_origin, const guest_size_t<Word>* dst_origin, const guest_size_t<Word>* region, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueCopyImageToBuffer(::av::ClientTransport& t, uint64_t command_queue, uint64_t src_image, uint64_t dst_buffer, const guest_size_t<Word>* src_origin, const guest_size_t<Word>* region, guest_size_t<Word> dst_offset, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueCopyBufferToImage(::av::ClientTransport& t, uint64_t command_queue, uint64_t src_buffer, uint64_t dst_image, guest_size_t<Word> src_offset, const guest_size_t<Word>* dst_origin, const guest_size_t<Word>* region, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
uint64_t av_clCreateProgramWithSource(::av::ClientTransport& t, uint64_t context, cl_uint count, const char** strings, const guest_size_t<Word>* lengths, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateProgramWithBinary(::av::ClientTransport& t, uint64_t context, cl_uint num_devices, const uint64_t* device_list, const guest_size_t<Word>* lengths, const unsigned char** binaries, cl_int* binary_status, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clRetainProgram(::av::ClientTransport& t, uint64_t program);
template <typename Word = size_t>
cl_int av_clReleaseProgram(::av::ClientTransport& t, uint64_t program);
template <typename Word = size_t>
cl_int av_clBuildProgram(::av::ClientTransport& t, uint64_t program, cl_uint num_devices, const uint64_t* device_list, const char* options);
template <typename Word = size_t>
cl_int av_clGetProgramInfo(::av::ClientTransport& t, uint64_t program, cl_program_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clGetProgramBuildInfo(::av::ClientTransport& t, uint64_t program, uint64_t device, cl_program_build_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clUnloadPlatformCompiler(::av::ClientTransport& t, uint64_t platform);
template <typename Word = size_t>
uint64_t av_clCreateKernel(::av::ClientTransport& t, uint64_t program, const char* kernel_name, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clCreateKernelsInProgram(::av::ClientTransport& t, uint64_t program, cl_uint num_kernels, uint64_t* kernels, cl_uint* num_kernels_ret);
template <typename Word = size_t>
cl_int av_clRetainKernel(::av::ClientTransport& t, uint64_t kernel);
template <typename Word = size_t>
cl_int av_clReleaseKernel(::av::ClientTransport& t, uint64_t kernel);
template <typename Word = size_t>
cl_int av_clSetKernelArg(::av::ClientTransport& t, uint64_t kernel, cl_uint arg_index, guest_size_t<Word> arg_size, const void* arg_value);
template <typename Word = size_t>
cl_int av_clGetKernelInfo(::av::ClientTransport& t, uint64_t kernel, cl_kernel_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clGetKernelArgInfo(::av::ClientTransport& t, uint64_t kernel, cl_uint arg_index, cl_kernel_arg_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clGetKernelWorkGroupInfo(::av::ClientTransport& t, uint64_t kernel, uint64_t device, cl_kernel_work_group_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clEnqueueNDRangeKernel(::av::ClientTransport& t, uint64_t command_queue, uint64_t kernel, cl_uint work_dim, const guest_size_t<Word>* global_work_offset, const guest_size_t<Word>* global_work_size, const guest_size_t<Word>* local_work_size, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueMarkerWithWaitList(::av::ClientTransport& t, uint64_t command_queue, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueBarrierWithWaitList(::av::ClientTransport& t, uint64_t command_queue, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clWaitForEvents(::av::ClientTransport& t, cl_uint num_events, const uint64_t* event_list);
template <typename Word = size_t>
cl_int av_clGetEventInfo(::av::ClientTransport& t, uint64_t event, cl_event_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
uint64_t av_clCreateUserEvent(::av::ClientTransport& t, uint64_t context, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clSetUserEventStatus(::av::ClientTransport& t, uint64_t event, cl_int execution_status);
template <typename Word = size_t>
cl_int av_clRetainEvent(::av::ClientTransport& t, uint64_t event);
template <typename Word = size_t>
cl_int av_clReleaseEvent(::av::ClientTransport& t, uint64_t event);
template <typename Word = size_t>
cl_int av_clGetEventProfilingInfo(::av::ClientTransport& t, uint64_t event, cl_profiling_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
uint64_t av_clCreateFromGLBuffer(::av::ClientTransport& t, uint64_t context, cl_mem_flags flags, cl_GLuint bufobj, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateFromGLTexture(::av::ClientTransport& t, uint64_t context, cl_mem_flags flags, cl_GLenum target, cl_GLint miplevel, cl_GLuint texture, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateFromGLTexture2D(::av::ClientTransport& t, uint64_t context, cl_mem_flags flags, cl_GLenum target, cl_GLint miplevel, cl_GLuint texture, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateFromGLTexture3D(::av::ClientTransport& t, uint64_t context, cl_mem_flags flags, cl_GLenum target, cl_GLint miplevel, cl_GLuint texture, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateFromGLRenderbuffer(::av::ClientTransport& t, uint64_t context, cl_mem_flags flags, cl_GLuint renderbuffer, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clGetGLObjectInfo(::av::ClientTransport& t, uint64_t memobj, cl_gl_object_type* gl_object_type, cl_GLuint* gl_object_name);
template <typename Word = size_t>
cl_int av_clGetGLTextureInfo(::av::ClientTransport& t, uint64_t memobj, cl_gl_texture_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clEnqueueAcquireGLObjects(::av::ClientTransport& t, uint64_t command_queue, cl_uint num_objects, const uint64_t* mem_objects, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueReleaseGLObjects(::av::ClientTransport& t, uint64_t command_queue, cl_uint num_objects, const uint64_t* mem_objects, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clGetGLContextInfoKHR(::av::ClientTransport& t, const uint64_t* properties, cl_gl_context_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clRetainDevice(::av::ClientTransport& t, uint64_t device);
template <typename Word = size_t>
cl_int av_clReleaseDevice(::av::ClientTransport& t, uint64_t device);
template <typename Word = size_t>
cl_int av_clCreateSubDevices(::av::ClientTransport& t, uint64_t in_device, const uint64_t* properties, cl_uint num_devices, uint64_t* out_devices, cl_uint* num_devices_ret);
template <typename Word = size_t>
cl_int av_clEnqueueTask(::av::ClientTransport& t, uint64_t command_queue, uint64_t kernel, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueMarker(::av::ClientTransport& t, uint64_t command_queue, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueBarrier(::av::ClientTransport& t, uint64_t command_queue);
template <typename Word = size_t>
cl_int av_clEnqueueWaitForEvents(::av::ClientTransport& t, uint64_t command_queue, cl_uint num_events, const uint64_t* event_list);
template <typename Word = size_t>
cl_int av_clEnqueueMigrateMemObjects(::av::ClientTransport& t, uint64_t command_queue, cl_uint num_mem_objects, const uint64_t* mem_objects, cl_mem_migration_flags flags, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueReadBufferRect(::av::ClientTransport& t, uint64_t command_queue, uint64_t buffer, cl_bool blocking_read, const guest_size_t<Word>* buffer_origin, const guest_size_t<Word>* host_origin, const guest_size_t<Word>* region, guest_size_t<Word> buffer_row_pitch, guest_size_t<Word> buffer_slice_pitch, guest_size_t<Word> host_row_pitch, guest_size_t<Word> host_slice_pitch, void* ptr, guest_size_t<Word> ptr_bytes, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueWriteBufferRect(::av::ClientTransport& t, uint64_t command_queue, uint64_t buffer, cl_bool blocking_write, const guest_size_t<Word>* buffer_origin, const guest_size_t<Word>* host_origin, const guest_size_t<Word>* region, guest_size_t<Word> buffer_row_pitch, guest_size_t<Word> buffer_slice_pitch, guest_size_t<Word> host_row_pitch, guest_size_t<Word> host_slice_pitch, const void* ptr, guest_size_t<Word> ptr_bytes, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueCopyBufferRect(::av::ClientTransport& t, uint64_t command_queue, uint64_t src_buffer, uint64_t dst_buffer, const guest_size_t<Word>* src_origin, const guest_size_t<Word>* dst_origin, const guest_size_t<Word>* region, guest_size_t<Word> src_row_pitch, guest_size_t<Word> src_slice_pitch, guest_size_t<Word> dst_row_pitch, guest_size_t<Word> dst_slice_pitch, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueFillImage(::av::ClientTransport& t, uint64_t command_queue, uint64_t image, const void* fill_color, guest_size_t<Word> fill_color_bytes, const guest_size_t<Word>* origin, const guest_size_t<Word>* region, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
uint64_t av_clCreateImage2D(::av::ClientTransport& t, uint64_t context, cl_mem_flags flags, const cl_image_format* image_format, guest_size_t<Word> image_width, guest_size_t<Word> image_height, guest_size_t<Word> image_row_pitch, void* host_ptr, guest_size_t<Word> host_ptr_bytes, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateImage3D(::av::ClientTransport& t, uint64_t context, cl_mem_flags flags, const cl_image_format* image_format, guest_size_t<Word> image_width, guest_size_t<Word> image_height, guest_size_t<Word> image_depth, guest_size_t<Word> image_row_pitch, guest_size_t<Word> image_slice_pitch, void* host_ptr, guest_size_t<Word> host_ptr_bytes, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clUnloadCompiler(::av::ClientTransport& t);
template <typename Word = size_t>
cl_int av_clSetCommandQueueProperty(::av::ClientTransport& t, uint64_t command_queue, cl_command_queue_properties properties, cl_bool enable, cl_command_queue_properties* old_properties);
template <typename Word = size_t>
uint64_t av_clCreateProgramWithIL(::av::ClientTransport& t, uint64_t context, const void* il, guest_size_t<Word> length, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clCompileProgram(::av::ClientTransport& t, uint64_t program, cl_uint num_devices, const uint64_t* device_list, const char* options, cl_uint num_input_headers, const uint64_t* input_headers, const char** header_include_names);
template <typename Word = size_t>
uint64_t av_clLinkProgram(::av::ClientTransport& t, uint64_t context, cl_uint num_devices, const uint64_t* device_list, const char* options, cl_uint num_input_programs, const uint64_t* input_programs, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clSetProgramSpecializationConstant(::av::ClientTransport& t, uint64_t program, cl_uint spec_id, guest_size_t<Word> spec_size, const void* spec_value);
template <typename Word = size_t>
uint64_t av_clCloneKernel(::av::ClientTransport& t, uint64_t source_kernel, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clSetKernelExecInfo(::av::ClientTransport& t, uint64_t kernel, cl_kernel_exec_info param_name, guest_size_t<Word> param_value_size, const void* param_value);
template <typename Word = size_t>
cl_int av_clGetKernelSubGroupInfo(::av::ClientTransport& t, uint64_t kernel, uint64_t device, cl_kernel_sub_group_info param_name, guest_size_t<Word> input_value_size, const void* input_value, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
cl_int av_clGetHostTimer(::av::ClientTransport& t, uint64_t device, cl_ulong* host_timestamp);
template <typename Word = size_t>
cl_int av_clGetDeviceAndHostTimer(::av::ClientTransport& t, uint64_t device, cl_ulong* device_timestamp, cl_ulong* host_timestamp);
template <typename Word = size_t>
uint64_t av_clCreateBufferWithProperties(::av::ClientTransport& t, uint64_t context, const uint64_t* properties, cl_mem_flags flags, guest_size_t<Word> size, void* host_ptr, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateImageWithProperties(::av::ClientTransport& t, uint64_t context, const uint64_t* properties, cl_mem_flags flags, const cl_image_format* image_format, const guest_cl_image_desc<Word>* image_desc, uint64_t image_desc_buffer, void* host_ptr, guest_size_t<Word> host_ptr_bytes, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clCreateSamplerWithProperties(::av::ClientTransport& t, uint64_t context, const uint64_t* sampler_properties, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clSetDefaultDeviceCommandQueue(::av::ClientTransport& t, uint64_t context, uint64_t device, uint64_t command_queue);
template <typename Word = size_t>
uint64_t av_clCreateSampler(::av::ClientTransport& t, uint64_t context, cl_bool normalized_coords, cl_addressing_mode addressing_mode, cl_filter_mode filter_mode, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clRetainSampler(::av::ClientTransport& t, uint64_t sampler);
template <typename Word = size_t>
cl_int av_clReleaseSampler(::av::ClientTransport& t, uint64_t sampler);
template <typename Word = size_t>
cl_int av_clGetSamplerInfo(::av::ClientTransport& t, uint64_t sampler, cl_sampler_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
uint64_t av_clCreatePipe(::av::ClientTransport& t, uint64_t context, cl_mem_flags flags, cl_uint pipe_packet_size, cl_uint pipe_max_packets, const uint64_t* properties, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clGetPipeInfo(::av::ClientTransport& t, uint64_t pipe, cl_pipe_info param_name, guest_size_t<Word> param_value_size, void* param_value, guest_size_t<Word>* param_value_size_ret);
template <typename Word = size_t>
uint64_t av_clCreateProgramWithBuiltInKernels(::av::ClientTransport& t, uint64_t context, cl_uint num_devices, const uint64_t* device_list, const char* kernel_names, cl_int* errcode_ret);
template <typename Word = size_t>
uint64_t av_clEnqueueMapBuffer(::av::ClientTransport& t, uint64_t command_queue, uint64_t buffer, cl_bool blocking_map, cl_map_flags map_flags, guest_size_t<Word> offset, guest_size_t<Word> size, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event, cl_int* errcode_ret);
template <typename Word = size_t>
cl_int av_clEnqueueUnmapMemObject(::av::ClientTransport& t, uint64_t command_queue, uint64_t memobj, uint64_t mapped_ptr, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
uint64_t av_clSVMAlloc(::av::ClientTransport& t, uint64_t context, cl_svm_mem_flags flags, guest_size_t<Word> size, cl_uint alignment);
template <typename Word = size_t>
void av_clSVMFree(::av::ClientTransport& t, uint64_t context, uint64_t svm_pointer);
template <typename Word = size_t>
cl_int av_clEnqueueSVMMap(::av::ClientTransport& t, uint64_t command_queue, cl_bool blocking_map, cl_map_flags flags, uint64_t svm_ptr, guest_size_t<Word> size, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueSVMUnmap(::av::ClientTransport& t, uint64_t command_queue, uint64_t svm_ptr, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueSVMMemcpy(::av::ClientTransport& t, uint64_t command_queue, cl_bool blocking_copy, uint64_t dst_ptr, uint64_t src_ptr, guest_size_t<Word> size, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clEnqueueSVMMemFill(::av::ClientTransport& t, uint64_t command_queue, uint64_t svm_ptr, const void* pattern, guest_size_t<Word> pattern_size, guest_size_t<Word> size, cl_uint num_events_in_wait_list, const uint64_t* event_wait_list, uint64_t* event);
template <typename Word = size_t>
cl_int av_clSetKernelArgSVMPointer(::av::ClientTransport& t, uint64_t kernel, cl_uint arg_index, uint64_t arg_value);
template <typename Word = size_t>
cl_int av_clSetEventCallback(::av::ClientTransport& t, uint64_t event, cl_int command_exec_callback_type);

}  // namespace av::clproto
