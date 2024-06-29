# Sistema de Monitoreo de Cultivos Indoor

![Dashboard de Zabbix](./images/zabbix_dashboard.png)

## Descripción

Este sistema está diseñado para monitorear las condiciones ambientales de cultivos indoor utilizando sensores conectados a un ESP8266. Los datos se envían a un servidor Zabbix, donde se visualizan en un tablero de control.

## Sensores Utilizados

- **DHT22**: Sensor de temperatura y humedad.
- **Higrómetro**: Sensor de humedad del suelo.
  - **Higrómetro 0**
  - **Higrómetro 1**
- **Sensor de CO**: Sensor de dióxido de carbono (CO).

## Hardware

- **ESP8266**: Microcontrolador WiFi utilizado para conectar y controlar los sensores.
- **Módulo de Relé**: Utilizado para controlar dispositivos adicionales, como sistemas de riego.
- **Convertidor de Nivel Lógico**: Para asegurar la compatibilidad de voltaje entre el ESP8266 y los sensores.

## Instalación y Configuración

1. **Conexión del Hardware**:
   - Conecte el DHT22 al pin D1 del ESP8266.
   - Conecte los higrómetros a los pines S0, S1, S2 y S3 del ESP8266.
   - Conecte el sensor de CO a los pines S0, S1, S2 y S3 del ESP8266.
   - Conecte el relé al pin D2 del ESP8266.

2. **Configuración del Software**:
   - Clone este repositorio.
   - Configure las credenciales de WiFi en el archivo `main.cpp`.
   - Configure los detalles del servidor Zabbix:
     ```cpp
     const char* WIFI_SSID = "your_wifi_ssid"; // Tu SSID de WiFi
     const char* WIFI_PASSWORD = "your_wifi_password"; // Tu contraseña de WiFi
     String ZABBIX_URL = "http://your_zabbix_url/api_jsonrpc.php"; // La URL de tu servidor Zabbix
     String ZABBIX_TOKEN = "your_zabbix_token"; // Tu token de API de Zabbix
     String HOST_ID = "your_host_id"; // Tu ID de host de Zabbix
     ```
   - Compile y cargue el código en el ESP8266 utilizando la Arduino IDE o PlatformIO.

3. **Visualización en Zabbix**:
   - Ingrese al tablero de Zabbix y configure los widgets para visualizar los datos de los sensores.
   - Los datos de temperatura, humedad, humedad del suelo y CO se mostrarán en tiempo real.

## Uso

El sistema recogerá datos de los sensores y los enviará periódicamente al servidor Zabbix. Puede monitorear las condiciones del cultivo indoor y recibir alertas en caso de que los valores de los sensores superen los umbrales definidos.

## Diagrama del Hardware

A continuación se muestra el diagrama de conexión del hardware para configurar el sistema:

![Diagrama del Hardware](./images/hardware_diagram.png)

## Imagen del Tablero

![Dashboard de Zabbix](./images/zabbix_dashboard.png)

Esta imagen muestra un ejemplo de cómo se visualizan los datos de los sensores en el tablero de Zabbix.

## Contribuciones

Las contribuciones son bienvenidas. Por favor, haga un fork del repositorio y envíe un pull request con sus mejoras.

## Licencia

Este proyecto está licenciado bajo la Licencia MIT - consulte el archivo [LICENSE](LICENSE.md) para obtener más detalles.