# Weather Station Setup Guide

The weather station app requires:
1. WiFi credentials (SSID/password)
2. OpenWeatherMap API key (free tier available)

## Setup Steps

### 1. Copy the template config file
```bash
cp apps/weather/config.h.template src/config.h
```

Or use the example environment file:
```bash
cp .env.example .env
# Edit .env with your credentials
```

### 2. Edit `src/config.h` with your actual values:

```cpp
#define WIFI_SSID     "your_actual_wifi_ssid"
#define WIFI_PASSWORD "your_actual_password"

// OpenWeatherMap — get a free API key at https://openweathermap.org/api
#define OWM_API_KEY   "your_actual_api_key"
#define OWM_CITY      "Your City,ST,US"  // Format: "City,State,CountryCode"
```

### 3. Build and upload (in .venv)
```bash
source /home/scarolan/git_repos/cores3se-arduino/.venv/bin/activate
pio run -t upload --upload-port /dev/ttyACM0
```

## Obtaining an OpenWeatherMap API Key

1. Go to https://openweathermap.org/api
2. Sign up for a free account
3. Navigate to "API keys" in your account settings
4. Copy your API key (it may take 10-15 minutes to activate)
5. Paste it into `src/config.h`

## Troubleshooting

- **Connection failed**: Verify WiFi SSID and password are correct
- **No data received**: Check API key is valid and city name is properly formatted
- **Slow updates**: Current refresh interval is 10 minutes (defined in config.h)
