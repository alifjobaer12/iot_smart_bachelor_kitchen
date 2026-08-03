#ifndef SMART_KITCHEN_SECRETS_H
#define SMART_KITCHEN_SECRETS_H

// Copy this file to secrets.h and enter your local 2.4 GHz Wi-Fi details.
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Replace these with the HTTPS endpoints from your backend deployment.
const char* apiGetMeals = "https://YOUR_BACKEND_DOMAIN/api/meal";
const char* apiPostSensors = "https://YOUR_BACKEND_DOMAIN/api/sensors";
const char* apiPostCooking = "https://YOUR_BACKEND_DOMAIN/api/kitchen";

#endif
