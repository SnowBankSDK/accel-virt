#ifndef AV_IMAGE_RETIREMENT_HPP
#define AV_IMAGE_RETIREMENT_HPP

namespace av {

class ImageRetirement {
public:
    ImageRetirement(const ImageRetirement &) = delete;
    ImageRetirement &operator=(const ImageRetirement &) = delete;

    virtual void retired() = 0;

protected:
    ImageRetirement() = default;
    ~ImageRetirement() = default;
};

}

#endif
