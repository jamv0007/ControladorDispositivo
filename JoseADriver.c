

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/fcntl.h>



#define DRIVER_NAME	"JoseADriver"
#define DRIVER_CLASS "JoseADriverClass"
#define NUM_DEVICES	3  /* Número de dispositivos a crear */
#define DATA_SPACE   1024

static dev_t major_minor = -1;
static struct class *JoseAclass = NULL;

static struct Hour{
   int hour;
   int minutes;
   int seconds;
   int days;
};

bool writeMode = false;

//Datos de los devices
static struct devicesData{
   const char* name;
   int minor;
   struct cdev cdev;
   char data[DATA_SPACE];
   int dataSize;
}deviceDataList[NUM_DEVICES];

//Parametros
static int includeNumbers = 0;
module_param(includeNumbers,int,S_IWUSR);
MODULE_PARM_DESC(includeNumbers, "Incluye numeros en contraseña, 0 si y 1 no");

static int includeUp = 0;
module_param(includeUp,int,S_IWUSR);
MODULE_PARM_DESC(includeUp, "Incluye mayusculas en contraseña, 0 si y 1 no");

static int includeSpecials = 0;
module_param(includeSpecials,int,S_IWUSR);
MODULE_PARM_DESC(includeSpecials, "Incluye caracteres especiales en contraseña, 0 si y 1 no");

static int toBinary = 0;
module_param(toBinary,int,S_IWUSR);
MODULE_PARM_DESC(toBinary, "Indica 0 decimal a binario y 1 binario a decimal");


/* ============ Funciones que implementan las operaciones de apertura y liberacion del controlador ============= */

static int OpenFunction(struct inode *inode, struct file *file) {

   //Se guardan los datos para obtenerlos en el read y write para cada dispositivo
   struct devicesData* datos;

   datos = container_of(inode->i_cdev,struct devicesData,cdev);

   file->private_data = datos;

   return 0;
}

static int ReleaseFunction(struct inode *inode, struct file *file) {
   return 0;
}

/*===================Funciones auxiliares de la lectura y escritura de Timer==========================*/
//Eleva base a exponente
static int pow(int base, int exp){
    int sum = 1, i;

    for(i = 0; i < exp; i++){
        sum *= base;
    }

    return sum;
}
//Inicializa vector a 0
static void inicializeCharVector(char* v,int tam){
   for(int i = 0; i < tam; i++){
      v[i] = '\0';
   }
}

//Obtiene la hora del hardware
struct Hour getHour(void){

   unsigned int hour, minute, second;

   outb(0x04, 0x70); // Selecciona el registro de horas
   hour = inb(0x71);// Lee el valor del registro de horas
   
   outb(0x02, 0x70); // Selecciona el registro de minutos
   minute = inb(0x71); // Lee el valor del registro de minutos

   outb(0x00, 0x70); // Selecciona el registro de segundos
   second = inb(0x71); // Lee el valor del registro de segundos

   //Primero se aplica al numero una mascara para escoger los del primer cuarteto de bits,
   //y se desplaza 1 para que quede como cabecera en 4 bits y se repite desplazando 3 para que se obtengan dos valores binarios que al sumarlos de los primeros 4 bits
   //Se aplica por ultimo una mascara para obtener los 4 ultimos bits y se suma lo que da un numero que corresponde al decimal leido
   int h = ((hour & 0xF0) >> 1) + ((hour & 0xF0) >> 3) + (hour & 0xf);
   int m = ((minute & 0xF0) >> 1) + ((minute & 0xF0) >> 3) + (minute & 0xf);
   int s = ((second & 0xF0) >> 1) + ((second & 0xF0) >> 3) + (second & 0xf);

   struct Hour currentHour;
   currentHour.hour = h;
   currentHour.minutes = m;
   currentHour.seconds = s;
   
   return currentHour;
}

//Calcula la nueva hora con la actual y unos minutos
static struct Hour calculateHour(struct Hour currentHour,int dato){
   struct Hour newHour;
   newHour.hour = currentHour.hour;
   newHour.minutes = currentHour.minutes;
   newHour.seconds = currentHour.seconds;

   int days = 0;
   //Se incrementa los minutos, horas y dias
   for(int i = 0; i < dato; i++){
      newHour.minutes++;
      if(newHour.minutes == 60){
         newHour.minutes = 0;
         newHour.hour++;

         if(newHour.hour == 24){
            newHour.hour = 0;
         }
      }
      //Si completa 24 horas se añade un dia
      if((newHour.hour == currentHour.hour) && (newHour.minutes == currentHour.minutes)){
         days++;
      }
   }

   newHour.days = days;

   return newHour;
}

//Devuelve el formato de salida 
static int formatData(char* data,struct Hour hour,int c){

   int tam = 0;
   //Si no hay minutos y se lee devuelve la actual
   if(c){
      tam = sprintf(data,"La hora actual es: %i:%i:%i\n",hour.hour,hour.minutes,hour.seconds);
      return tam;
   }

   //Segun si han pasado dias o no
   if(hour.days > 0){
      tam = sprintf(data,"La nueva hora actual es: %i:%i:%i y %i dias\n",hour.hour,hour.minutes,hour.seconds,hour.days);
   }else{
      tam = sprintf(data,"La nueva hora actual es: %i:%i:%i\n",hour.hour,hour.minutes,hour.seconds);
   }

   return tam;
}

//Realizar el paso de espacio de kernel a usuario de los datos
static ssize_t sendData(char* h, int size, char __user *buffer, size_t count, loff_t *f_pos){
      //Si la posicion es mayor o igual que el tamaño a copiar, para
      if(*f_pos >= size){
         return 0;
      }
      //Se calcula los bytes que se pueden leer desde la posicion actual del vector
      if( size < count + *f_pos){
         count = size - *f_pos;
      }
      //Se pasa a espacio de usuario
      if(copy_to_user(buffer,h,count)){
         return -EFAULT;
      }
      //Se actualiza la posicion del puntero
      *f_pos += count;

      return count;
   
} 

/*==============================Funciones Read y Write de Timer================================*/
static ssize_t TimerRead(struct file *file, char __user *buffer, size_t count, loff_t *f_pos) {

   struct devicesData* data;
   data = file->private_data;

   //Si no hay datos al hacer el read se muestra la hora actual
   if(data->dataSize != 0){
      return sendData(data->data,data->dataSize,buffer,count,f_pos);
   }else{
      //Sino se muestra la hora nueva
      struct Hour c = getHour();
      inicializeCharVector(data->data,sizeof(data->data));
      int tam = formatData(data->data,c,true);
      return sendData(data->data,tam,buffer,count,f_pos);
   }
   
   return 0;
}

static ssize_t TimerWrite(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos) {

   char data[DATA_SPACE];

   if(copy_from_user(data, buffer, count)){
      pr_err("Maximo 1024 bytes\n");
      return -EINVAL;
   }

   //Se calcula que el input sea numeros solo
   int length = count -1;
   int number = 0;
   bool incorrectInput = false;
   for(int i = 0; i < length && !incorrectInput; i++){
      int asciiCode = (int) data[i];
      if(asciiCode < 48 || asciiCode > 57){
         incorrectInput = true;
      }

      if(incorrectInput){
         pr_err("Entrada incorrecta: Tiene caracteres no numericos");
         return -EINVAL;
      }else{
         //Se convierte a numero, primero se resta 0 para pasarlo a decimal y luego segun la posicion se calcula el numero 
         number += (data[i]-'0') * pow(10,length-i-1);
      }
   }

   if(!incorrectInput){
      struct devicesData* data;
      data = file->private_data;
      if(number == 0){
         data->dataSize = 0;
      }else{
         struct Hour currentHour = getHour();
         //Se calcula la nueva hora 
         struct Hour newHour = calculateHour(currentHour,number);
         //Se envia a memoria
         inicializeCharVector(data->data,DATA_SPACE);
         data->dataSize = formatData(data->data,newHour,false);
         if(data->dataSize < 0){
            data->dataSize = 0;
            pr_err("No hay espacio en memoria");
            return -EINVAL;
         }
         
      }
   }  

   return count;
}

/*==============Funciones auxiliares de Pass==============================*/

//Convierte el numero a positivo
static int positiveNumber(int value){
   int res = value;
   if(value < 0){
      res = res * -1;
   }
   return res;
}

//Genera contraseña aleatoria
static void generatePassword(char* pass, int passwordTam){

   
   char numbers[] = "0123456789";
   char letter[] = "abcdefghijklmnoqprstuvwyzx";
   char LETTER[] = "ABCDEFGHIJKLMNOQPRSTUYWVZX";
   char symbols[] = "!@#$^&*?";

   int random;
   for(int i = 0; i < passwordTam; i++){
      pr_info("%i\n",i);
      //Numero aleatorio
      get_random_bytes(&random,sizeof(random));
      //Se realiza modulo 4 para cada grupo
      random = random % 4;
      //Se convierte a positivo
      random = positiveNumber(random);

      //Se incrementa el grupo si no esta permitido el uso de ese grupo
      bool is = false;
      while(!is){
         switch (random)
         {
            case 0:
               if(includeNumbers){
                  random++;
               }else{
                  is = true;
               }
               break;

            case 1:
               if(includeSpecials){
                  random++;
               }else{
                  is = true;
               }
               break;

            case 2:
               if(includeUp){
                  random++;
               }else{
                  is = true;
               }
               break;

            default:
               is = true;
               break;
         }
      }
      
      //Para el grupo se genera un numero dentro del grupo y se añade
      int element;
      get_random_bytes(&element,sizeof(element));
      if(random == 0){
         element = element % 10;
         pass[i] = numbers[positiveNumber(element)];
      }else if(random == 1){
         element = element % 8;
         pass[i] = symbols[positiveNumber(element)];
      }else if(random == 2){
         element = element % 26;
         pass[i] = LETTER[positiveNumber(element)];
      }else if(random == 3){
         element = element % 26;
         pass[i] = letter[positiveNumber(element)];
      }
   }

   
}



/*==============Funciones de read y write para Pass=======================*/


static ssize_t PassRead(struct file *file, char __user *buffer, size_t count, loff_t *f_pos) {

   struct devicesData* dev;
   dev = file->private_data;

   return sendData(dev->data,dev->dataSize,buffer,count,f_pos);

}


static ssize_t PassWrite(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos) {
   
   char data[DATA_SPACE];

   if(copy_from_user(data, buffer, count)){
      pr_err("Maximo 1024 bytes\n");
      return -EINVAL;
   }

   int length = count -1;
   int number = 0;
   bool incorrectInput = false;
   for(int i = 0; i < length && !incorrectInput; i++){
      int asciiCode = (int) data[i];
      if(asciiCode < 48 || asciiCode > 57){
         incorrectInput = true;
      }

      if(incorrectInput){
         pr_err("Entrada incorrecta: Tiene caracteres no numericos\n");
         return -EINVAL;
      }else{
         number += (data[i]-'0') * pow(10,length-i-1);
      }
   }

   if(!incorrectInput){
      if(number > 0 && number < DATA_SPACE){         
         struct devicesData* data;
         data = file->private_data;
         
         char pass[DATA_SPACE];
         inicializeCharVector(pass,DATA_SPACE);
         generatePassword(pass,number);
         
         inicializeCharVector(data->data,DATA_SPACE);
         data->dataSize = sprintf(data->data,"%s\n",pass);
   
      }else{
         return -EINVAL;
      }
   } 

   return count;
}

/*===============Funciones auxiliares de Binario==========================*/
//Calcula el numero de bits de un numero al convertirlo a binario
static int calculateBits(long number,int max){
   pr_info("%i\n",max);
   //Se calcula 2 elevado a i hasta que i sea mayor que el numero o llegue al limite almacenado
   for(int i = 1; i < max; i++){
      if(pow(2,i)-1 >= number){
         return i;
      }
   }

   return -1;
}

//Calcula el numero en binario
static bool convertToBinary(long number, char* binary,int size){
   //Se calculan los bits
   int bits = calculateBits(number,size);
   if(bits != -1){
      int pos = 0;//Posicion
      int div = number;//Dividendo actual
      int last = 0;// Obtener el ultimo dividendo
      //Se divide entre 2 y se almacenan los restos
      while( div >= 2){
         last = div;
         binary[bits-1-pos] = (div % 2) + '0';
         div = div / 2;
         pos++;
      }

      binary[0] = (last / 2) + '0';//Se obtiene el ultimo cociente
      
      return true;
   }

   return false;
}

//Calcular tamaño 
static int calculateTam(long number){
   int exp = 1;
   while(pow(10,exp) <= number){
      exp++;
   }

   return exp;
}

//Convetir de numero a array de numeros
static void convertNumberToArray(long number,int* convertNumber,int tam){
   
   int div = number;
   int pos = 0;
   int last = 0;

   //Se divide entre 10 hasta que no se pueda y nos quedamos con los restos y el ultimo cociente
   while(div >= 10){
      last = div;
      convertNumber[tam-1-pos] = div % 10;
      div = div / 10;
      pos++;
   }

   convertNumber[0] = last / 10;

}

//Convertir de binario a decimal
static bool convertToDecimal(long number,char* binary,int size){
   //Se calcula el tamaño del numero en decimal(numero de elementos del numero)
   int tam = calculateTam(number);
   int cnumber[tam];
   int res = 0;
   //Se convierte a array
   convertNumberToArray(number,cnumber,tam);
   //Para cada numero se multiplica por 2 elevado a la posicion y se suma
   for(int i = 0; i < tam; i++){
      res += cnumber[i] * pow(2,tam-1-i); 
   }

   sprintf(binary,"%i",res);

   return true;
   
}

/*=============== Funciones de Read y Write para Conversion a binario=============*/

static ssize_t BinaryRead(struct file *file, char __user *buffer, size_t count, loff_t *f_pos) {
   struct devicesData* dev;
   dev = file->private_data;

   return sendData(dev->data,dev->dataSize,buffer,count,f_pos);
}

static ssize_t BinaryWrite(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos) {

   char data[DATA_SPACE];

   if(copy_from_user(data, buffer, count)){
      pr_err("Maximo 1024 bytes\n");
      return -EINVAL;
   }

   int length = count -1;
   long number = 0;
   bool incorrectInput = false;
   for(int i = 0; i < length && !incorrectInput; i++){
      int asciiCode = (int) data[i];

      if(toBinary == 0){
         if(asciiCode < 48 || asciiCode > 57){
            incorrectInput = true;
         }
      }else{
         if(asciiCode < 48 || asciiCode > 49){
            incorrectInput = true;
         }
      }
      if(incorrectInput){
         pr_err("Entrada incorrecta: Tiene caracteres no numericos\n");
         return -EINVAL;
      }else{
         number += (data[i]-'0') * pow(10,length-i-1);
      }
   }

   if(!incorrectInput){
      if(number > -1){
         struct devicesData* data;
         data = file->private_data;
         char p[DATA_SPACE];
         inicializeCharVector(p,DATA_SPACE);
         //Si es convertir a binario
         if(toBinary == 0){
            if(!convertToBinary(number,p,DATA_SPACE)){
               pr_err("Numero en binario supera la memoria");
               return -EINVAL;
            }
            
            inicializeCharVector(data->data,DATA_SPACE);
            data->dataSize = sprintf(data->data,"El numero %i en binario es: %s\n",number,p);
         }else{
            if(!convertToDecimal(number,p,DATA_SPACE)){
               pr_err("Numero en decimal supera la memoria");
               return -EINVAL;
            }
            inicializeCharVector(data->data,DATA_SPACE);
            data->dataSize = sprintf(data->data,"El numero %i en decimal es: %s\n",number,p);
         }

         if(data->dataSize < 0){
            data->dataSize = 0;
            pr_err("No hay espacio en memoria");
            return -EINVAL;
         }
      }

   } 


   return count;
}
/* ============ Estructura con las operaciones del controlador ============= */

static const struct file_operations Timer_fops = {
   .owner = THIS_MODULE,
   .open = OpenFunction,
   .read = TimerRead,
   .write = TimerWrite,
   .release = ReleaseFunction,
};

static const struct file_operations Pass_fops = {
   .owner = THIS_MODULE,
   .open = OpenFunction,
   .read = PassRead,
   .write = PassWrite,
   .release = ReleaseFunction,
};

static const struct file_operations Binary_fops = {
   .owner = THIS_MODULE,
   .open = OpenFunction,
   .read = BinaryRead,
   .write = BinaryWrite,
   .release = ReleaseFunction,
};

static struct devices{
   const char* name;
   const struct file_operations *fops;
}deviceList[NUM_DEVICES] = {
   [0] = {"Timer",&Timer_fops},
   [1] = {"Pass",&Pass_fops},
   [2] = {"Binary",&Binary_fops}
};

/* ============ Inicialización del controlador ============= */

static int JoseADev_uevent(struct device *dev, struct kobj_uevent_env *env) {
   add_uevent_var(env, "DEVMODE=%#o", 0666);
   return 0;
}

static int __init init_driver(void) {
    int n_device;
    dev_t id_device;

    if (alloc_chrdev_region(&major_minor, 0, NUM_DEVICES, DRIVER_NAME) < 0) {
       pr_err("Major number assignment failed");
       goto error;
    }

    /* En este momento el controlador tiene asignado un "major number"
       Podemos consultarlo mirando en /proc/devices */
    pr_info("%s driver assigned %d major number\n", DRIVER_NAME, MAJOR(major_minor));

    if((JoseAclass = class_create(THIS_MODULE, DRIVER_CLASS)) == NULL) {
       pr_err("Class device registering failed");
       goto error;
    } else
       JoseAclass->dev_uevent = JoseADev_uevent; /* Evento para configurar los permisos de acceso */

    /* En este momento la clase de dispositivo aparece en /sys/class */
    pr_info("/sys/class/%s class driver registered\n", DRIVER_CLASS);

    for (n_device = 0; n_device < NUM_DEVICES; n_device++) {
      cdev_init(&deviceDataList[n_device].cdev, deviceList[n_device].fops);
      
      deviceDataList[n_device].name = deviceList[n_device].name;
      id_device = MKDEV(MAJOR(major_minor), MINOR(major_minor) + n_device);
      deviceDataList[n_device].minor = id_device;
      deviceDataList[n_device].dataSize = 0;
      if(cdev_add(&deviceDataList[n_device].cdev, id_device, 1) == -1) {
         pr_err("Device node creation failed");
         goto error;
      }

      if(device_create(JoseAclass, NULL, id_device, NULL, DRIVER_NAME "%s", deviceList[n_device].name) == NULL) {
         pr_err("Device node creation failed");
         goto error;
      }

      pr_info("Device node /dev/%s%s created\n", DRIVER_NAME, deviceList[n_device].name);
    }

    /* En este momento en /dev aparecerán los dispositivos y en /sys/class/ también */

    pr_info("Alarm driver initialized and loaded\n");
    return 0;

error:
    if(JoseAclass) 
       class_destroy(JoseAclass);

    if(major_minor != -1)
       unregister_chrdev_region(major_minor, NUM_DEVICES);

    return -1;
}

/* ============ Descarga del controlador ============= */

static void __exit exit_driver(void) {
    int n_device;

    for (n_device = 0; n_device < NUM_DEVICES; n_device++) {
       device_destroy(JoseAclass, MKDEV(MAJOR(major_minor), MINOR(major_minor) + n_device));
       cdev_del(&deviceDataList[n_device].cdev);
    }
    
    class_destroy(JoseAclass);

    unregister_chrdev_region(major_minor, NUM_DEVICES);

    pr_info("Alarm driver unloaded\n");
}

/* ============ Meta-datos ============= */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jose Antonio");
MODULE_VERSION("0.1");
MODULE_DESCRIPTION("Skeleton of a full device driver module");

module_init(init_driver)
module_exit(exit_driver)