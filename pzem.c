#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <curl/curl.h>
#include </usr/include/modbus/modbus.h>
#include <json-c/json.h>
#include <time.h>

#ifndef __USE_TIME_BITS64
struct timespec {
	time_t tv_sec;
	long tv_nsec;
};
#endif

#include <gpiod.h>

#define SLAVE_ID 1
#define PORT "/dev/ttyAMA0"
#define BAUD_RATE 9600
#define GPIO 17

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t data_size = size * nmemb;
    char *response_buffer = (char *)userdata;
    
    // Append the received data to the response buffer
    strncat(response_buffer, ptr, data_size);
    
    return data_size;
}

int main() 
{
	struct gpiod_chip *chip;
	struct gpiod_line *line;
	int ret;
	int toggled;

	modbus_t *mb;
	uint16_t data[10];
	int rc;

	chip = gpiod_chip_open("/dev/gpiochip4");
	if (!chip)
	{
		perror("failed to open GPIO chip\n");
		return 1;
	}

	line = gpiod_chip_get_line(chip, GPIO);
	if (!line)
	{
		perror("failed to get GPIO line\n");
		gpiod_chip_close(chip);
		return 1;
	}

	ret = gpiod_line_request_output(line, "gpio_toggle", 0);
	if (ret < 0)
	{
		perror("failed to request gpio line\n");
		gpiod_chip_close(chip);
		return 1;
	}

	// Create a new Modbus RTU context
	mb = modbus_new_rtu(PORT, BAUD_RATE, 'N', 8, 1);
	if (mb == NULL) 
	{
		fprintf(stderr, "Failed to create the Modbus context: %s\n", modbus_strerror(errno));
		return -1;
	}
	
	printf("successfully initialized modbus...\n");
	// Set the slave ID
	modbus_set_slave(mb, SLAVE_ID);

	// Connect to the PZEM-004T module
	if (modbus_connect(mb) == -1)
	{
		fprintf(stderr, "Failed to connect to pzem: %s\n", modbus_strerror(errno));
		modbus_free(mb);
		return -1;
	}
	printf("successfully connected to pzem...\n");

	CURL *curl_post =  curl_easy_init();
	if (curl_post == NULL)
	{
		fprintf(stderr, "Failed to initialize CURL post\n");
		exit(1);
	}

	CURL *curl_get = curl_easy_init();
	if (curl_get == NULL)
	{
		fprintf(stderr, "failed to ininitialize CURL get\n");
		exit(1);
	}

	while (1)
	{
		// Read input registers
		rc = modbus_read_input_registers(mb, 0, 10, data);
		if (rc == -1)
		{
			fprintf(stderr, "Failed to read input registers: %s\n", modbus_strerror(errno));
			break;
		}

		// Extract and process the data
		float voltage = data[0] / 10.0;
		float current_A = (data[1] + (data[2] << 16)) / 1000.0;
		float power_W = (data[3] + (data[4] << 16)) / 10.0;

		// Create a json object and populate the new object
		json_object *json_data = json_object_new_object();

		json_object_object_add(json_data, "power", json_object_new_double(power_W));
		json_object_object_add(json_data, "voltage", json_object_new_double(voltage));
		json_object_object_add(json_data, "current", json_object_new_double(current_A));

		// convert our json data to a json string
		const char *json_string = json_object_to_json_string(json_data);
		printf("%s\n", json_string);
		if (curl_post) 
		{
			CURLcode res;
			struct curl_slist *headers = NULL;
			headers = curl_slist_append(headers, "Content-Type: application/json");

			// Set url of the post endpoint 
			curl_easy_setopt(curl_post, CURLOPT_URL, "10.103.1.88:8080/power");

			// Set HTTP method to post
			curl_easy_setopt(curl_post, CURLOPT_POST, 1L);

			// Set the json data as the POST data
			curl_easy_setopt(curl_post, CURLOPT_POSTFIELDS, json_string);

			// Set the headers
			curl_easy_setopt(curl_post, CURLOPT_HTTPHEADER, headers);

			// Perform post
			res= curl_easy_perform(curl_post);

			if (res != CURLE_OK)
			{
				fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			}

			curl_slist_free_all(headers);
			json_object_put(json_data);
		}

		if (curl_get)
		{
			CURLcode res;
			long http_code = 0;
			char response_buffer[1024] = {0};

			// Set the URL of the GET endpoint
			curl_easy_setopt(curl_get, CURLOPT_URL, "10.103.1.88:8080/toggle-device");

			// Set the HTTP method to get
			curl_easy_setopt(curl_get, CURLOPT_HTTPGET, 1L);

			curl_easy_setopt(curl_get, CURLOPT_WRITEFUNCTION, write_callback);
			curl_easy_setopt(curl_get, CURLOPT_WRITEDATA, response_buffer);

			res = curl_easy_perform(curl_get);
				
				
			if (res == CURLE_OK)
			{
				curl_easy_getinfo(curl_get, CURLINFO_RESPONSE_CODE, &http_code);

				if (http_code == 200)
				{
					json_object *response_json = json_tokener_parse(response_buffer);
					if (response_json != NULL)
					{
						json_object *toggle_value;
						if (json_object_object_get_ex(response_json, "toggle", &toggle_value))
						{
							if (json_object_get_boolean(toggle_value))
							{
								toggled = !toggled;
								ret = gpiod_line_set_value(line, toggled ? 1 : 0);
								if (ret < 0)
								{
									perror("failed to set line value\n");
									break;
								}
								printf("Relay toggled: %s\n", toggled ? "ON" : "OFF");
							}
						}
					}
					json_object_put(response_json);
				}
			}
		
		}
		else
		{
			printf("get failed\n");
		}

		sleep(1);
	}

	// Close the Modbus connection
	modbus_close(mb);
	modbus_free(mb);
	curl_easy_cleanup(curl_post);
	curl_easy_cleanup(curl_get);
	gpiod_line_release(line);
	gpiod_chip_close(chip);

	return 0;
}

