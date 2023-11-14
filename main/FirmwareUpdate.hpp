#ifndef FIRMWARE_UPDATE
#define FIRMWARE_UPDATE
#pragma once

#include "IFileUpload.hpp"
#include "esp_ota_ops.h"

class FirmwareUpdate: public IFileUploadHandler
{
    int current_id = -1;
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = nullptr;

    size_t data_size;
    size_t was_recorded = 0;
    bool image_header_was_checked = false;

    void begin();
    void write(size_t len, uint8_t *data);
    void end();

    void check();
public:
    FirmwareUpdate(IFileUpload* src);

    void upload_handler(int id, size_t size, size_t len, uint8_t *data) override;
};

#endif