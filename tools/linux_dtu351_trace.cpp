// Linux-only trace helper for comparing the official DekTec DTAPI/Dtu driver
// receive sequence with the macOS user-space implementation.
#include "CDTAPI.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static bool check(unsigned int result, const char* operation)
{
    std::fprintf(stderr, "%s: %u (%s)\n", operation, result,
                 DtapiResult2Str(result));
    return result == DTAPI_OK;
}

int main(int argc, char** argv)
{
    int dtapi_rx_mode = DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B;
    if (argc == 2 && std::strcmp(argv[1], "16b") == 0)
        dtapi_rx_mode = DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B;
    else if (argc != 1 && (argc != 2 || std::strcmp(argv[1], "10b") != 0)) {
        std::fprintf(stderr, "usage: %s [10b|16b]\n", argv[0]);
        return 64;
    }
    DtHwFuncDesc functions[32] = {};
    int function_count = 0;
    unsigned int result = DtapiHwFuncScan(32, &function_count, functions);
    if (!check(result, "DtapiHwFuncScan"))
        return 1;

    std::printf("hardware functions: %d\n", function_count);
    const DtHwFuncDesc* selected = nullptr;
    for (int i = 0; i < function_count; ++i) {
        const DtHwFuncDesc& fn = functions[i];
        std::printf("[%d] device=%s description=%s serial=%lld port=%d "
                    "sdi=%d input=%d output=%d\n",
                    i, fn.DeviceName, fn.Description,
                    static_cast<long long>(fn.SerialNumber), fn.Port,
                    fn.IsSdi, fn.IsInput, fn.IsOutput);
        if (selected == nullptr && fn.IsSdi && fn.IsInput)
            selected = &fn;
    }
    if (selected == nullptr) {
        std::fprintf(stderr, "No SDI input function found\n");
        return 2;
    }

    DtDevice* device = DtDevice_Alloc();
    DtInpChannel* input = DtInpChannel_Alloc();
    if (device == nullptr || input == nullptr) {
        std::fprintf(stderr, "DTAPI allocation failed\n");
        return 3;
    }

    int exit_code = 1;
    bool device_attached = false;
    bool input_attached = false;
    do {
        result = DtDevice_AttachToSerial(device, selected->SerialNumber);
        if (!check(result, "DtDevice_AttachToSerial"))
            break;
        device_attached = true;

        result = DtDevice_SetToInput(device, selected->Port);
        if (!check(result, "DtDevice_SetToInput"))
            break;

        int video_standard = DTAPI_VIDSTD_UNKNOWN;
        for (int attempt = 0; attempt < 50; ++attempt) {
            result = DtDevice_DetectVidStd(device, selected->Port,
                                           &video_standard);
            std::fprintf(stderr, "DetectVidStd[%d]: %u (%s), standard=%d\n",
                         attempt, result, DtapiResult2Str(result),
                         video_standard);
            if (result == DTAPI_OK &&
                video_standard != DTAPI_VIDSTD_UNKNOWN)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (video_standard == DTAPI_VIDSTD_UNKNOWN) {
            std::fprintf(stderr, "No video standard detected\n");
            break;
        }

        result = DtDevice_SetIoConfig(device, selected->Port,
                                      DTAPI_IOCONFIG_IOSTD,
                                      DTAPI_IOCONFIG_HDSDI,
                                      DTAPI_IOCONFIG_1080I59_94);
        if (!check(result, "DtDevice_SetIoConfig(1080i59.94)"))
            break;

        result = DtInpChannel_AttachToPort(input, device, selected->Port);
        if (!check(result, "DtInpChannel_AttachToPort"))
            break;
        input_attached = true;

        result = DtInpChannel_SetRxControl(input, DTAPI_RXCTRL_IDLE);
        if (!check(result, "DtInpChannel_SetRxControl(IDLE)"))
            break;
        result = DtInpChannel_ClearFifo(input);
        if (!check(result, "DtInpChannel_ClearFifo"))
            break;
        result = DtInpChannel_SetRxMode(input, dtapi_rx_mode);
        if (!check(result, "DtInpChannel_SetRxMode(SDI_FULL)"))
            break;
        result = DtInpChannel_SetRxControl(input, DTAPI_RXCTRL_RCV);
        if (!check(result, "DtInpChannel_SetRxControl(RCV)"))
            break;

        int max_fifo = 0;
        result = DtInpChannel_GetMaxFifoSize(input, &max_fifo);
        std::fprintf(stderr, "GetMaxFifoSize: %u (%s), bytes=%d\n",
                     result, DtapiResult2Str(result), max_fifo);

        std::vector<char> frame(16 * 1024 * 1024);
        int frame_size = static_cast<int>(frame.size());
        result = DtInpChannel_ReadFrame(input, frame.data(), &frame_size, 5000);
        std::fprintf(stderr, "ReadFrame: %u (%s), bytes=%d\n",
                     result, DtapiResult2Str(result), frame_size);

        int flags = 0;
        int latched = 0;
        unsigned int flags_result =
            DtInpChannel_GetFlags(input, &flags, &latched);
        std::fprintf(stderr,
                     "GetFlags: %u (%s), current=0x%08x latched=0x%08x\n",
                     flags_result, DtapiResult2Str(flags_result), flags,
                     latched);

        if (result != DTAPI_OK)
            break;
        std::printf("CAPTURE_OK standard=%d bytes=%d first16=", video_standard,
                    frame_size);
        for (int i = 0; i < 16 && i < frame_size; ++i)
            std::printf("%02x", static_cast<unsigned char>(frame[i]));
        std::printf("\n");
        exit_code = 0;
    } while (false);

    if (input_attached) {
        DtInpChannel_SetRxControl(input, DTAPI_RXCTRL_IDLE);
        DtInpChannel_Detach(input, 0);
    }
    if (device_attached)
        DtDevice_Detach(device);
    DtInpChannel_Free(input);
    DtDevice_Free(device);
    return exit_code;
}
