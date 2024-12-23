# Alarma de Incendios
## Introducción
Mi proyecto consistirá en armar un sistema de alarma de incendios, que sea suficientemente confiable para ser utilizado en un ambiente de trabajo. Esto requerirá una serie de funciones que aprovecharán las posibilidades del ESP-32.

## Objetivos
1. Responder a fuegos sin demoras significativas.
   
2. Alertar, de formas visibles, a las personas en riesgo.
   
3. Permitir acceso rápido al estado del sistema a través de la red.
   
## Alcances
- Mantener y actualizar una página web. Para acceder, hay que conectarse a la red del módulo ESP32.

- Monitorear el estado de una sola habitación.

- Alimentado con una batería de 9V, tendrá una autonomía limitada.

## Funcionalidad
El sistema tomará en cuenta las lecturas tanto de un sensor infrarrojo como de un detector de humo para su lógica. En caso de activarse cualquiera de los sensores, el sistema activará su estado de alarma. Se encenderán el buzzer y el led rojo, y se enviarán alertas a la red. De lo contrario, se encenderá el led verde, y se enviarán actualizaciones a la red en intervalos regulares.

## Propuestas a futuro
El sistema podría ser adaptado para controlar múltiples sistemas, distintas oficinas en un edificio, por ejemplo. Sería posible, entonces, monitorear el estado de cada una, y en caso de incendio, controlar el avance del fuego. Esto se podría combinar con otras aplicaciones, como sistemas de vigilancia o control de ambiente, tanto para la mejor prevención de accidentes como para un espacio de trabajo más cómodo y saludable. 

## Más información
En las carpetas del repositorio se encuentra más información del proyecto, y los archivos necesarios para hacerlo funcionar.
