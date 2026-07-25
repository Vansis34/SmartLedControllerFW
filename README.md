# LED Controller FW

Прошивка двухканального LED-контроллера для ESP32. Управление выполняется
через встроенную веб-страницу; алгоритмы свечения реализованы на периферии
LEDC и FreeRTOS.

## Среда сборки

Проект собирается в PlatformIO с фреймворком ESP-IDF.

- PlatformIO Espressif32: 6.5.0;
- ESP-IDF: 5.1.2;
- Xtensa GCC: 12.2.0;
- плата: generic ESP32 Dev Module;
- flash: 2 MB.




## Конфигурация ESP-IDF

Текущий `sdkconfig` сохранён и используется PlatformIO. Открыть его
интерактивную конфигурацию можно командой:

```powershell
pio run --target menuconfig
```



Веб-страница хранится в `main/www/index.html`, а прошивка отдаёт её gzip-копию
`main/www/index.html.gz`. PlatformIO автоматически создаёт детерминированную
gzip-копию перед сборкой и не перезаписывает её, если HTML не изменился.

