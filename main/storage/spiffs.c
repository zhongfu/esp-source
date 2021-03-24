#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_spiffs.h>

static const char *TAG = "spiffs";

esp_err_t spiffs_init(void) {
    ESP_LOGI(TAG, "Initializing certs spiffs partition");

    esp_vfs_spiffs_conf_t certs_conf = {
        .base_path = "/certs",
        .partition_label = "certs",
        .max_files = 5, // wifi CA, cadet CA, aws iot CA + wifi user cert/key?
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&certs_conf);

    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(err));
        }
    } else {
        size_t total = 0, used = 0;
        err = esp_spiffs_info(certs_conf.partition_label, &total, &used);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
        }
    }

    return err;
}

int spiffs_write(char *partition, char *filename, char *data, size_t data_sz) {
    ESP_LOGI(TAG, "Writing %d bytes to /%s/%s", data_sz, partition, filename);
    struct stat st;

    size_t path_len = 1 + strlen(partition) + 1 + strlen(filename) + 1; // "/" + partition + "/" + filename + null
    char *path = malloc(path_len);

    snprintf(path, path_len, "/%s", partition);
    if (stat(path, &st) != 0) { // /partition not there, partition doesn't exist?
        ESP_LOGE(TAG, "Failed to find partition %s while writing to spiffs!", partition);
        return 1;
    }

    snprintf(path, path_len, "/%s/%s", partition, filename);
    if (stat(path, &st) == 0) { // file exists. check if they're the same to avoid unnecessary flash writes
        if (st.st_size == data_sz) {
            ESP_LOGI(TAG, "File %s already exists, and sizes match!", path);
            FILE *old_file = fopen(path, "r");
            char *old_data = malloc(st.st_size);
            fread(old_data, st.st_size, 1, old_file);
            fclose(old_file);

            int res = memcmp(old_data, data, data_sz);
            free(old_data);

            if (res == 0) {
                ESP_LOGI(TAG, "Old file %s matches new content, skipping write", path);
                free(path);
                return 0;
            }
        }

        // if we're here, then either the sizes don't match or the file contents are different
        // so we delete
        ESP_LOGI(TAG, "Deleting old file at %s...", path);
        unlink(path);
    }

    // now write the file
    FILE *file = fopen(path, "w");
    fwrite(data, data_sz, 1, file);
    int err = ferror(file);
    if (err != 0) {
        ESP_LOGE(TAG, "Got error %d while writing to %s", err, path);
        return err;
    }
    fclose(file);
    free(path);
    return 0;
}

/**
 * puts data in *data and size in *data_sz
 * 
 * returns 1 if file not found
 * returns something else if it's... some other error from ferror
 * returns 0 if successful
 */

int spiffs_read(char *partition, char *filename, char *data, size_t *data_sz) {
    ESP_LOGI(TAG, "Reading contents of /%s/%s", partition, filename);
    struct stat st;

    size_t path_len = 1 + strlen(partition) + 1 + strlen(filename) + 1; // "/" + partition + "/" + filename + null
    char *path = malloc(path_len);

    snprintf(path, path_len, "/%s/%s", partition, filename);
    if (stat(path, &st) == 0) { // file exists, yay!
        *data_sz = st.st_size;
        data = malloc(st.st_size);
        FILE *file = fopen(path, "r");
        fread(data, *data_sz, 1, file);
        int err = ferror(file);
        if (err != 0) {
            ESP_LOGE(TAG, "Got error %d while reading from %s", err, path);
            free(data);
            free(path);
            return err;
        }
        fclose(file);
    } else { // file doesn't exist :/
        ESP_LOGE(TAG, "File %s doesn't exist :/", path);
        free(path);
        return 1;
    }

    free(path);
    return 0;
}

int spiffs_delete(char *partition, char *filename) {
    ESP_LOGI(TAG, "Deleting /%s/%s", partition, filename);
    struct stat st;

    size_t path_len = 1 + strlen(partition) + 1 + strlen(filename) + 1; // "/" + partition + "/" + filename + null
    char *path = malloc(path_len);

    snprintf(path, path_len, "/%s/%s", partition, filename);
    if (stat(path, &st) == 0) { // file exists, yay!
        unlink(path);
    } else { // file doesn't exist :/
        ESP_LOGE(TAG, "File %s doesn't exist :/", path);
        free(path);
        return 1;
    }

    free(path);
    return 0;
}