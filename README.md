# ControladorDispositivo

## 1. Descripción de los controladores de dispositivo
Se han implementado para esta práctica un driver (JoseADriver) que controla 3 dispositivos con funciones distintas. Las funciones y características de los dispositivos implementados son los siguientes:
### 1.1 JoseADriverTimer
Este dispositivo permite obtener la hora actual accediendo al RTC y modificar la hora mostrada con un numero definido de minutos. De esta forma, al realizar una llamada de read al dispositivo sin realizar ninguna operación de write sobre el mismo, se obtiene la hora real del RTC.
Al realizar una operación de write con un echo, pasando un numero de minutos, al realizar una operación de read, se obtiene la hora actual sumando esa cantidad de minutos, y además en caso de que el numero de minutos haga que la hora sobrepase las 24 horas, también indicará como salida el numero de días trascurridos desde la hora actual.
Cada vez que se haga un read se obtiene esa hora ya calculada, por lo que para obtener una nueva hay que volver a realizar el write y el read nuevamente.
Además esta función se puede resetear para obtener la hora actual de nuevo. Para ello, a la hora de pasarle un numero de minutos, si este es 0, al realizar un read se obtiene de nuevo la hora actual.
Es útil si se quiere obtener la hora actual y si se quiere calcular la hora que será pasados x minutos de forma rápida.
### 1.2 JoseADriverPass
Este dispositivo permite generar contraseñas aleatorias de una longitud definida. Al realizar un write con un número se generará una contraseña aleatoria de esa longitud y al realizar una operación de read la obtenemos por pantalla. Las contraseñas generadas pueden contener letras minúsculas, mayúsculas, números y símbolos especiales. Para controlar esto en la instalación del driver hay 3 parámetros que permiten indicar si se permiten mayúsculas, números o símbolos especiales:
includeNumbers=0
includeUp=0
includeSpecials=0
Donde 0 indica que se incluye y 1 que no se incluyen.
Esta contraseña se mantiene almacenada y se devuelve con el read hasta que se realice otra operación write al dispositivo.
Este driver es útil si se necesita generar una contraseña para acceder y no se nos ocurre alguna buena o de una dificultad (que contenga números, mayúsculas, y longitud determinada por ejemplo).
### 1.3. JoseADriverBinary
Este dispositivo permite convertir de decimal a binario y viceversa. Al realizar un write pasando un numero decimal, este lo convierte a binario y lo muestra por pantalla al realizar un read. El valor es igual hasta que se repita el write. También permite pasar un numero en binario y este lo convierte a decimal al mostrarlo con el read. Para escoger la conversión se ha habilitado un parámetro al realizar instalación del controlador:
toBinary=1
Donde 0 indica de decimal a binario y 1 de binario a decimal.
Además para evitar errores se comprueba que no se incluyan otros números que no sean 0 o 1 en binario.
Este controlador puede ser útil si se necesita convertir de decimal a binario o viceversa y no se quiere realizar operaciones a mano o no se puede acceder a internet a ningun conversor online.

## 2. Prueba de funcionamiento
Para instalar el controlador se usa el siguiente comando con los parámetros incluidos:
sudo insmod JoseADriver.ko includeNumbers=0 includeUp=0 includeSpecials=0 toBinary=0
### 2.1 JoseADriverTimer
Se puede obtener la hora actual del RTC con el read:

<img width="886" alt="Captura de pantalla 2023-09-25 a las 14 09 50" src="https://github.com/jamv0007/ControladorDispositivo/assets/84525141/5bd18684-389e-4b73-873c-f0b5353eff9e">

Para obtener la hora con x minutos se realiza un echo al device y se usa un cat para realizar el read del dispositivo:

<img width="882" alt="Captura de pantalla 2023-09-25 a las 14 10 25" src="https://github.com/jamv0007/ControladorDispositivo/assets/84525141/03b9747b-0379-450f-b1c0-3c8795dbb1be">

Si se pasa de las 24 horas, se muestra el numero de dias:

<img width="881" alt="Captura de pantalla 2023-09-25 a las 14 10 58" src="https://github.com/jamv0007/ControladorDispositivo/assets/84525141/aa691bad-12af-4f71-9e67-7c6f5f0389be">

Se puede resetear si se realiza un echo 0

<img width="886" alt="Captura de pantalla 2023-09-25 a las 14 11 28" src="https://github.com/jamv0007/ControladorDispositivo/assets/84525141/17876ddc-a9b4-48a4-aa2d-83589838da46">

## 2.2 JoseADriverPass
Al realizar un echo con un numero que indica la longitud y realizar un cat se obtiene la contraseña:

<img width="885" alt="Captura de pantalla 2023-09-25 a las 14 12 20" src="https://github.com/jamv0007/ControladorDispositivo/assets/84525141/e3445621-fdae-41e5-b08b-5d24b44c3765">

Por ejemplo a la hora de instalar el driver indicamos que solo se permiten numeros y minusculas(Por defecto):

<img width="881" alt="Captura de pantalla 2023-09-25 a las 14 12 48" src="https://github.com/jamv0007/ControladorDispositivo/assets/84525141/ff7c91f3-6753-499e-a5bb-7c33f0a4289e">

### 2.3 JoseADriverBinary
Al pasarle un numero decimal para conversión a binario, se realiza el echo con el numero en decimal y con el cat obtenemos el numero en binario:

<img width="882" alt="Captura de pantalla 2023-09-25 a las 14 13 51" src="https://github.com/jamv0007/ControladorDispositivo/assets/84525141/60a1ba62-26cc-45a3-8a08-b946ea237a82">

Si al instalarlo se cambia el parámetro para convertir a decimal. Si el numero no es binario devuelve error y si es correcto al realizar una lectura se devuelve el valor:

<img width="883" alt="Captura de pantalla 2023-09-25 a las 14 14 21" src="https://github.com/jamv0007/ControladorDispositivo/assets/84525141/2be67ae8-3d3a-4094-816b-2eb7d545e7d7">

## 3. Observaciones
Cabe destacar que la memoria de almacenamiento del driver es limitada a 1024 caracteres por lo que ese es el limite de input para los echo en todos los dispositivos. Para la hora recibirá como máximo un numero que contenga 1024 caracteres. Para la contraseña igual para la longitud por lo que esta solo podrá ser de 1023 como máximo.
Para la conversión de binario a decimal, el numero binario debe tener menos de 1024 y en el caso de decimal a binario, el numero en binario no podrá sobrepasar esta longitud.
También hay que indicar que si un controlador pide un numero, pasarle caracteres devolverá un error de argumento invalido, así como se ha visto antes a la hora de pasar un valor binario, también esta restringido a 1 o 0.



