#include "FirmwareUpdate.hpp"
#include "esp_log.h"
#include <cstring>
#include "esp_system.h"

#define TAG "Firmware Update"

FirmwareUpdate::FirmwareUpdate(IFileUpload* src)
{
    check();

    if (src)
        src->RegisterUploadHandler(this, &IFileUploadHandler::upload_handler);
}

void FirmwareUpdate::upload_handler(int id, size_t size, size_t len, uint8_t *data)
{
    // ESP_LOGI(TAG, "%d %d %d", id, size, len);
    if (current_id < 0) {
        current_id = id;
        data_size = size;

        begin();

        // for (int i=0; i<len; ++i)
        //  printf("%c", data[i]);

    }

    assert(id == current_id);

    write(len, data);

    if (size == was_recorded) {
        end();
        current_id = -1;
    }
}

void FirmwareUpdate::begin()
{
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    ESP_LOGI(TAG, "Starting OTA example task");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08lx, but running from offset 0x%08lx",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08lx)",
             running->type, running->subtype, running->address);

    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != 0);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx",
             update_partition->subtype, update_partition->address);
}

void FirmwareUpdate::write(size_t len, uint8_t *data)
{
    esp_err_t err;

    // ESP_LOGE(TAG, "%d %d", len, (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)));

    if (image_header_was_checked == false) {
        esp_app_desc_t new_app_info;
        if (len > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
            // check current version with downloading
            memcpy(&new_app_info, &data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
            ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

            const esp_partition_t *running = esp_ota_get_running_partition();
            esp_app_desc_t running_app_info;
            if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
            }

            const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
            esp_app_desc_t invalid_app_info;
            if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
                ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
            }

            // check current version with last invalid partition
            if (last_invalid_app != NULL) {
                if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
                    ESP_LOGW(TAG, "New version is the same as invalid version.");
                    ESP_LOGW(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
                    ESP_LOGW(TAG, "The firmware has been rolled back to the previous version.");
                    // http_cleanup(client);
                    // infinite_loop();
                }
            }
// #ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
//                 if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
//                     ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
//                 }
// #endif

            image_header_was_checked = true;

            err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                esp_ota_abort(update_handle);
                // error = true;
                return;
            }
            ESP_LOGI(TAG, "esp_ota_begin succeeded");
        } else {
            ESP_LOGE(TAG, "received package is not fit len");
            esp_ota_abort(update_handle);
        }
    }


    err = esp_ota_write( update_handle, (const void *)data, len);
    if (err != ESP_OK) {
        esp_ota_abort(update_handle);
        // error = true;
        return;
    }
    was_recorded += len;
    ESP_LOGD(TAG, "Written image length %d", len);
}

void FirmwareUpdate::end()
{
    esp_err_t err;

    ESP_LOGI(TAG, "Total Write binary data length: %d", was_recorded);

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        return;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        return;
    }
    // ESP_LOGI(TAG, "Prepare to restart system!");
    // esp_restart();
}

void FirmwareUpdate::check()
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // run diagnostic function ...
            bool diagnostic_is_ok = true; //diagnostic();
            if (diagnostic_is_ok) {
                ESP_LOGI(TAG, "Diagnostics completed successfully! Continuing execution ...");
                esp_ota_mark_app_valid_cancel_rollback();
            } else {
                ESP_LOGE(TAG, "Diagnostics failed! Start rollback to the previous version ...");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
    }
}