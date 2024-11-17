# Componentes utilizados

+ Un módulo receptor infrarrojo con salida analógica y salida digital regulable.
  Detectará emisiones de luz en el espectro infrarrojo, y está construido para ser
  sensible a las emitidas por las llamas.

+ Un buzzer. Alertará al usuario en caso de incendio.

+ Dos LEDs de distintos colores, preferiblemente rojo y verde. Junto con el buzzer, permiten
  conocer el estado de alerta.

+ Módulo detector de humo, con un sensor MQ-2 integrado. Detecta GLP, propano, 
  metano, alcohol, hidrógeno y humo; siendo más sensible a los dos primeros y al hidrógeno.

+ Módulo ESP-32 con capacidad para comunicación inalámbrica con WIFI y 5 pines GPIO. Yo utilizo
  el kit ESP32 DEVKIT V1.

+ Logic Level Shifter de cuatro canales, necesario para permitir la comunicación entre los
  pines GPIO del ESP y los sensores.

+ Regulador de tensión LM7805, con salida de 5V.

+ Regulador de tensión ajustable LM317, con salida de 3,3V.

+ Resistencias de:
  - 1 kOhm
  - 1,64 kOhm
  - 100 Ohm
  - 115 Ohm

+ Capacitores:
  - 0,22 uF, polarizado
  - 0,1 uF, cerámico
