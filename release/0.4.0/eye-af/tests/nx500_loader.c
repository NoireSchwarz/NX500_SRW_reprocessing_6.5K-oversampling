#define FACE_SYMBOL "_ZN20CFaceDetection_Linux13FaceDetectionEPhiiii"
#define CHECK_VALUE 71
int test_face(void *, unsigned char *, int, int, int, int) __asm__(FACE_SYMBOL);

#ifdef TEST_FACE_PROVIDER
int test_face(void *owner, unsigned char *image, int width, int height, int tracking, int orientation)
{
    return owner == image && width == 528 && height == 352 && tracking == 2 && orientation == 3
           ? CHECK_VALUE : -2;
}

int test_face_provider_version(void) { return CHECK_VALUE; }
#else
extern int test_face_provider_version(void);

static void probe_trace(const char *text, unsigned len)
{
    register int fd asm("r0") = 2;
    register const char *data asm("r1") = text;
    register unsigned count asm("r2") = len;
    register unsigned call asm("r7") = 4;
    asm volatile("svc 0" : "+r"(fd) : "r"(data), "r"(count), "r"(call) : "memory");
}

#define TRACE(s) probe_trace(s "\n", sizeof(s))

int main(void)
{
    TRACE("arm_loader entered");
    if (test_face_provider_version() != CHECK_VALUE)
        return 2;
    unsigned char pixel = 0;
    TRACE("arm_loader calling face wrapper");
    if (test_face(&pixel, &pixel, 528, 352, 2, 3) != CHECK_VALUE)
        return 4;
    TRACE("arm_loader callback_result=71");
    return 0;
}
#endif
