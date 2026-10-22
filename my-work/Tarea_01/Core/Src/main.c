/**
 ******************************************************************************
 * @file    main.c
 * @author  Emanuel Llanos Diaz <ellanos@unal.edu.co>
 * @version V1.0
 * @brief   Contador con dos fotocompuertas y
 *          display 7 segmentos
 *          Taller V - Semana 6 - NUCLEO-F411RE
 ******************************************************************************
 *   DESCRIPCION DEL PROGRAMA
 ---------------------------------------------------------------------------
 * El sistema muestra un numero entre 0000 y 9999 en un display de 7
 * segmentos de 4 digitos. Dos fotocompuertas modifican ese numero: U4 lo
 * aumenta en 1 y U5 lo disminuye en 1. Un LED de estado parpadea a 1 Hz
 * para indicar que el microcontrolador esta funcionando.
 *------------------------------------------------------------------------
 * MAPA DE PINES
 * -----------------------------------------------------------------------
 *
 * LED BLINKY 
 *   PH1   ->   LED VERDE BLINKY 
 *
 * Fotocompuertas 
 *   PB13  -> U4  (EXTI13)     
 *   PB14  -> U5  (EXTI14)     
 *
 * Display 7 segmentos 5461BS / CA56-12 (anodo comun)
 *   Segmentos              
 *     a   -> PB12   (pin 11 del display)
 *     b   -> PC5    (pin 7)
 *     c   -> PC8    (pin 4)
 *     d   -> PC9    (pin 2)
 *     e   -> PA11   (pin 1)
 *     f   -> PA12   (pin 10)
 *     g   -> PC6    (pin 5)
 *     DP  -> sin conectar (pin 3)
 *
 *   Digitos     
 *     Digito 1 (CA1, pin 12) -> PB9    (Q1)
 *     Digito 2 (CA2, pin 9)  -> PA5    (Q2)
 *     Digito 3 (CA3, pin 8)  -> PB6    (Q3)
 *     Digito 4 (CA4, pin 6)  -> PA7    (Q4)         
 *
 *          a
 *        -----
 *     f |     | b
 *       |  g  |
 *        -----
 *     e |     | c
 *       |     |
 *        -----
 *          d
 
    FUNCIONAMIENTO
 * ------------------------------------------------------------------------------
 * 1. Las ISR verifican su propia bandera de hardware, la limpian
 *    y levantan una bandera volatile (blinky_flag, display_flag,
 *    fotocompuerta_U4_flag, fotocompuerta_U5_flag).
 * 2. El ciclo principal revisa esas banderas, cuando alguna se levanta 
 *    ejecuta la funcion correspondiente:
 *    - led_toggle(): invierte el LED de estado haciendolo parpadear a 1 Hz.
 *    - fotocompuerta_contador(): suma o resta 1 al contador. Al pasar de
 *      9999 vuelve a 0000, y al bajar de 0000 vuelve a 9999.
 *    - display_refrescar(): multiplexa el display y lo refresca a 240Hz
 *    - secreto_evento(): muestra "Good" en vez del numero si ambas fotocompuertas se activan casi al mismo tiempo.
 * 3. Multiplexado: en cada interrupcion de TIM10 se enciende
 *    un solo digito. Para evitar el "fantasma" se sigue el orden
 *    apagar digitos -> cambiar segmentos -> encender el digito siguiente.
 *    El numero se separa en cifras con / y %. para que cada digito se muestre la cifra correspondiente.
 *    
 *
 *
 *      /\_/\
 *     ( o.o )  zzz... esperando EXTI
 *      > ^ <
 ******************************************************************************
 */
#include <stdint.h>
#include "stm32f411xe.h"
#include "stm32f4xx.h"

static void led_blinky_init(void);
static void led_toggle(void);
static void tim10_init(void);
static void tim11_init(void);
static void display_init(void);             
static void display_segments_init(void);     
static void display_digits_init(void);   
static void display_refrescar(void);    
static void display_seleccionar_digito(uint8_t digito);
static void display_seleccionar_numero(uint8_t numero);
static void fotocompuerta_init(void);
static void fotocompuerta_contador(void);
static void secreto_evento(uint8_t fotocompuerta);
static void secreto_tick(void);
static void display_seleccionar_letra(char letra);


volatile uint32_t blinky_flag = 0;
volatile uint32_t display_flag = 0;
volatile uint8_t fotocompuerta_U4_flag=0;
volatile uint8_t fotocompuerta_U5_flag=0;
uint16_t valor = 0; // Variable global para almacenar el valor a mostrar en el display


#define SECRETO_VENTANA_TICKS  46     /* ~200 ms: ventana para considerar "al mismo tiempo" */
#define SECRETO_GOOD_TICKS     465    /* Good */
uint8_t  secreto_ultima = 0;          /* ultima fotocompuerta que se activo (4 = U4, 5 = U5, 0 = ninguna) */
uint16_t secreto_ventana = 0;         /* ticks que quedan de la ventana */
uint16_t secreto_good = 0;            /* ticks que quedan mostrando "Good" (0 = modo normal) */
int main(void)
{

    led_blinky_init();
    display_init();
    fotocompuerta_init();
    tim10_init();
    tim11_init();
    
    /* Loop forever */
	while (1)
    {
      led_toggle();
      fotocompuerta_contador();
      display_refrescar();
    }
    return (0);
}


void TIM1_TRG_COM_TIM11_IRQHandler(void)
{

    if(TIM11->SR & TIM_SR_UIF) // Verificar si la interrupción fue causada por TIM11
    {
        /* Limpiar la bandera de interrupción */
        TIM11->SR &= ~TIM_SR_UIF;
        /* Levantar la bandera para el blinky */
        blinky_flag = 1;
    }
}

void TIM1_UP_TIM10_IRQHandler (void)
{
    if(TIM10->SR & TIM_SR_UIF) // Verificar si la interrupción fue causada por TIM10
    {
        /* Limpiar la bandera de interrupción */
        TIM10->SR &= ~TIM_SR_UIF;
        /* Levantar la bandera para el display */
        display_flag = 1;
    }
}

void EXTI15_10_IRQHandler(void)
{
    if (EXTI->PR & EXTI_PR_PR13) // Verificar si la interrupción fue causada por EXTI13
    {
        /* Limpiar la bandera de interrupción */
        EXTI->PR = EXTI_PR_PR13;
        fotocompuerta_U4_flag = 1;
    }

    if (EXTI->PR & EXTI_PR_PR14) // Verificar si la interrupción fue causada por EXTI14
    {
        /* Limpiar la bandera de interrupción */
        EXTI->PR = EXTI_PR_PR14;
        fotocompuerta_U5_flag = 1;
    }
}

static void led_blinky_init(void)
{
    /* Encender el reloj del GPIOH */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOHEN;
    /* Configurar PH1 como salida general push-pull */
    GPIOH->MODER &= ~GPIO_MODER_MODE1;  // Limpiar bits de modo
    GPIOH->MODER |= GPIO_MODER_MODE1_0; // Configurar como salida (01)
    GPIOH->PUPDR &= ~GPIO_PUPDR_PUPD1;   // Sin pull-up ni pull-down
    GPIOH->OTYPER &= ~GPIO_OTYPER_OT1;   // Configurar como push-pull
    GPIOH->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED1; // Limpiar bits de velocidad y velocidad baja

}

static void led_toggle(void)
{
      if (blinky_flag)
        {
            blinky_flag = 0;
            GPIOH->ODR ^= GPIO_ODR_OD1; // Cambiar el estado del LED
        }
}


static void tim10_init(void)
{
    /* Encender el reloj del TIM10 */
    RCC->APB2ENR |= RCC_APB2ENR_TIM10EN;
    /* Configurar prescaler y ARR, con reloj de 16 MHz eso da 240Hz de refresco para la interrucion, ya que 4x60Hz =240Hz*/
    TIM10->PSC = 1600 - 1; // Prescaler
    TIM10->ARR = 43 - 1;  // Autoreload 
    /* Habilitar la interrupción de actualización */
    TIM10->DIER |= TIM_DIER_UIE;
    

    NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn); // Matricular la interrupción en el NVIC

    /* Habilitar el temporizador */
    TIM10->CR1 |= TIM_CR1_CEN;
}
static void tim11_init(void)
{
    /* Encender el reloj del TIM11 */
    RCC->APB2ENR |= RCC_APB2ENR_TIM11EN;
    /* Configurar prescaler y ARR, con reloj de 16 MHz nos da 500 ms*/
    TIM11->PSC = 16000 - 1; // Prescaler
    TIM11->ARR = 500 - 1;  // Autoreload 
    /* Habilitar la interrupción de actualización */
    TIM11->DIER |= TIM_DIER_UIE;
    

    NVIC_EnableIRQ(TIM1_TRG_COM_TIM11_IRQn); // Matricular la interrupción en el NVIC

    /* Habilitar el temporizador */
    TIM11->CR1 |= TIM_CR1_CEN;
}

static void display_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN; // Habilitar reloj para GPIOA
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN; // Habilitar reloj para GPIOB
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN; // Habilitar reloj para GPIOC
    display_segments_init();
    display_digits_init();
}

static void display_segments_init(void)
{
    GPIOB->BSRR = GPIO_BSRR_BS12; // Inicializar PB12 en alto (segmento a apagado)
    GPIOC->BSRR = GPIO_BSRR_BS5;  // Inicializar PC5 en alto (segmento b apagado)
    GPIOC->BSRR = GPIO_BSRR_BS8;  // Inicializar PC8 en alto (segmento c apagado)
    GPIOC->BSRR = GPIO_BSRR_BS9;  // Inicializar PC9 en alto (segmento d apagado)
    GPIOA->BSRR = GPIO_BSRR_BS11; // Inicializar PA11 en alto (segmento e apagado)
    GPIOA->BSRR = GPIO_BSRR_BS12; // Inicializar PA12 en alto (segmento f apagado)
    GPIOC->BSRR = GPIO_BSRR_BS6;  // Inicializar PC6 en alto (segmento g apagado)

    GPIOB->MODER &= ~GPIO_MODER_MODE12;  // Limpiar bits para PB12
    GPIOC->MODER &= ~GPIO_MODER_MODE5;   // Limpiar bits para PC5
    GPIOC->MODER &= ~GPIO_MODER_MODE8;   // Limpiar bits para PC8
    GPIOC->MODER &= ~GPIO_MODER_MODE9;   // Limpiar bits para PC9
    GPIOA->MODER &= ~GPIO_MODER_MODE11;  // Limpiar bits para PA11
    GPIOA->MODER &= ~GPIO_MODER_MODE12;  // Limpiar bits para PA12
    GPIOC->MODER &= ~GPIO_MODER_MODE6;   // Limpiar bits para PC6

    GPIOB->MODER |= GPIO_MODER_MODE12_0; // Configurar PB12 como salida
    GPIOC->MODER |= GPIO_MODER_MODE5_0;  // Configurar PC5 como salida
    GPIOC->MODER |= GPIO_MODER_MODE8_0;  // Configurar PC8 como salida
    GPIOC->MODER |= GPIO_MODER_MODE9_0;  // Configurar PC9 como salida
    GPIOA->MODER |= GPIO_MODER_MODE11_0; // Configurar PA11 como salida
    GPIOA->MODER |= GPIO_MODER_MODE12_0; // Configurar PA12 como salida
    GPIOC->MODER |= GPIO_MODER_MODE6_0;  // Configurar PC6 como salida

    GPIOB->OTYPER &= ~GPIO_OTYPER_OT12; // Configurar PB12 como push-pull
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT5;  // Configurar PC5 como push-pull
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT8;  // Configurar PC8 como push-pull
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT9;  // Configurar PC9 como push-pull
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT11; // Configurar PA11 como push-pull
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT12; // Configurar PA12 como push-pull
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT6;  // Configurar PC6 como push-pull

    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED12; // Limpiar bits de velocidad y velocidad baja para PB12
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED5;  // Limpiar bits de velocidad y velocidad baja para PC5
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED8;  // Limpiar bits de velocidad y velocidad baja para PC8
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED9;  // Limpiar bits de velocidad y velocidad baja para PC9
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED11; // Limpiar bits de velocidad y velocidad baja para PA11
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED12; // Limpiar bits de velocidad y velocidad baja para PA12
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED6;  // Limpiar bits de velocidad y velocidad baja para PC6
}

static void  display_digits_init(void)
{
    GPIOB->BSRR = GPIO_BSRR_BS9;  // Inicializar PB9 en alto (digito 1 apagado)
    GPIOA->BSRR = GPIO_BSRR_BS5;  // Inicializar PA5 en alto (digito 2 apagado)
    GPIOB->BSRR = GPIO_BSRR_BS6;  // Inicializar PB6 en alto (digito 3 apagado)
    GPIOA->BSRR = GPIO_BSRR_BS7;  // Inicializar PA7 en alto (digito 4 apagado)

    GPIOB->MODER &= ~GPIO_MODER_MODE9;   // Limpiar bits para PB9
    GPIOA->MODER &= ~GPIO_MODER_MODE5;   // Limpiar bits para PA5
    GPIOB->MODER &= ~GPIO_MODER_MODE6;   // Limpiar bits para PB6
    GPIOA->MODER &= ~GPIO_MODER_MODE7;   // Limpiar bits para PA7

    GPIOB->MODER |= GPIO_MODER_MODE9_0;  // Configurar PB9 como salida
    GPIOA->MODER |= GPIO_MODER_MODE5_0;  // Configurar PA5 como salida
    GPIOB->MODER |= GPIO_MODER_MODE6_0;  // Configurar PB6 como salida
    GPIOA->MODER |= GPIO_MODER_MODE7_0;  // Configurar PA7 como salida

    GPIOB->OTYPER &= ~GPIO_OTYPER_OT9;  // Configurar PB9 como push-pull
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;  // Configurar PA5 como push-pull
    GPIOB->OTYPER &= ~GPIO_OTYPER_OT6;  // Configurar PB6 como push-pull
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT7;  // Configurar PA7 como push-pull

    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED9; // Limpiar bits de velocidad y velocidad baja para PB9
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED5; // Limpiar bits de velocidad y velocidad baja para PA5
    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED6; // Limpiar bits de velocidad y velocidad baja para PB6
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED7; // Limpiar bits de velocidad y velocidad baja para PA7
}

static void display_refrescar(void)
{
    static uint8_t digito_actual = 1;
    uint8_t cifra = 0;
    

    /* El default es en 0 para apagar los digitos*/
if (display_flag)
    {
        display_flag = 0;
        secreto_tick();                         /* FUNCION SECRETA: avanzar los contadores de tiempo */
        display_seleccionar_digito(0);       

   /*Dividir el numero en partes para mostrarlo en los digitos*/
   switch (digito_actual)
    {
        case 1:
            cifra = valor / 1000;               /* miles */
            break;
        case 2:
            cifra = (valor / 100) % 10;         /* centenas */
            break;
        case 3:
            cifra = (valor / 10) % 10;          /* decenas */
            break;
        case 4:
            cifra = valor % 10;                 /* unidades */
            break;
        default:
            cifra = 0;                          /* default */
            break;
    }
    
    if (secreto_good > 0)                   /* mostrar "Good" en vez del numero */
    {
        switch (digito_actual)
        {
            case 1:
                display_seleccionar_letra('G');
                break;
            case 2:
                display_seleccionar_letra('o');
                break;
            case 3:
                display_seleccionar_letra('o');
                break;
            case 4:
                display_seleccionar_letra('d');
                break;
            default:
                break;
        }
    }
    else
    {
        display_seleccionar_numero(cifra);
    }
    display_seleccionar_digito(digito_actual);

    digito_actual = (digito_actual % 4) + 1;
}
}
static void display_seleccionar_digito(uint8_t digito)
{
    /*Apagar todos los dígitos para evitar el fantasma*/
    GPIOB->BSRR = GPIO_BSRR_BS9 | GPIO_BSRR_BS6;  // Digito 1 y 3 apagado
    GPIOA->BSRR = GPIO_BSRR_BS7 | GPIO_BSRR_BS5;  // Digito 4 y 2 apagado

    /*Encender el dígito seleccionado segun el caso*/
    switch (digito)
    {
        case 1:
            GPIOB->BSRR = GPIO_BSRR_BR9; // Encender dígito 1
            break;
        case 2:
            GPIOA->BSRR = GPIO_BSRR_BR5; // Encender dígito 2
            break;
        case 3:
            GPIOB->BSRR = GPIO_BSRR_BR6; // Encender dígito 3
            break;
        case 4:
            GPIOA->BSRR = GPIO_BSRR_BR7; // Encender dígito 4
            break;
        default:
            break;
    }
}

static void display_seleccionar_numero(uint8_t numero)
{
    /*Apagar todos los segmentos para evitar el fantasma*/
    GPIOB->BSRR = GPIO_BSRR_BS12; // Segmento a y g apagados
    GPIOC->BSRR = GPIO_BSRR_BS5 | GPIO_BSRR_BS8 | GPIO_BSRR_BS9 | GPIO_BSRR_BS6;  // Segmento b, c d y g apagados
    GPIOA->BSRR = GPIO_BSRR_BS12 | GPIO_BSRR_BS11; // Segmento f y e apagados

    /*Encender los segmentos correspondientes al número seleccionado*/
    switch (numero)
    {
        case 0:
            GPIOB->BSRR = GPIO_BSRR_BR12; // Encender segmento a
            GPIOC->BSRR = GPIO_BSRR_BR5;  // Encender segmento b
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            GPIOC->BSRR = GPIO_BSRR_BR9;  // Encender segmento d
            GPIOA->BSRR = GPIO_BSRR_BR11; // Encender segmento e
            GPIOA->BSRR = GPIO_BSRR_BR12; // Encender segmento f
            break;
        case 1:
            GPIOC->BSRR = GPIO_BSRR_BR5;  // Encender segmento b
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            break;
        case 2:
            GPIOB->BSRR = GPIO_BSRR_BR12; // Encender segmento a
            GPIOC->BSRR = GPIO_BSRR_BR5;  // Encender segmento b
            GPIOC->BSRR = GPIO_BSRR_BR9;  // Encender segmento d
            GPIOA->BSRR = GPIO_BSRR_BR11; // Encender segmento e
            GPIOC->BSRR = GPIO_BSRR_BR6;  // Encender segmento g
            break;
        case 3:
            GPIOB->BSRR = GPIO_BSRR_BR12; // Encender segmento a
            GPIOC->BSRR = GPIO_BSRR_BR5;  // Encender segmento b
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            GPIOC->BSRR = GPIO_BSRR_BR9;  // Encender segmento d
            GPIOC->BSRR = GPIO_BSRR_BR6;  // Encender segmento g
            break;
        case 4:
            GPIOC->BSRR = GPIO_BSRR_BR5;  // Encender segmento b
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            GPIOA->BSRR = GPIO_BSRR_BR12; // Encender segmento f
            GPIOC->BSRR = GPIO_BSRR_BR6;  // Encender segmento g
            break;
        case 5:
            GPIOB->BSRR = GPIO_BSRR_BR12; // Encender segmento a
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            GPIOC->BSRR = GPIO_BSRR_BR9;  // Encender segmento d
            GPIOA->BSRR = GPIO_BSRR_BR12; // Encender segmento f
            GPIOC->BSRR = GPIO_BSRR_BR6;  // Encender segmento g
            break;
        case 6:
            GPIOB->BSRR = GPIO_BSRR_BR12; // Encender segmento a
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            GPIOC->BSRR = GPIO_BSRR_BR9;  // Encender segmento d
            GPIOA->BSRR = GPIO_BSRR_BR11; // Encender segmento e
            GPIOA->BSRR = GPIO_BSRR_BR12; // Encender segmento f
            GPIOC->BSRR = GPIO_BSRR_BR6;  // Encender segmento g
            break;
        case 7:
            GPIOB->BSRR = GPIO_BSRR_BR12; // Encender segmento a
            GPIOC->BSRR = GPIO_BSRR_BR5;  // Encender segmento b
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            break;
        case 8:
            GPIOB->BSRR = GPIO_BSRR_BR12; // Encender segmento a
            GPIOC->BSRR = GPIO_BSRR_BR5;  // Encender segmento b
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            GPIOC->BSRR = GPIO_BSRR_BR9;  // Encender segmento d
            GPIOA->BSRR = GPIO_BSRR_BR11; // Encender segmento e
            GPIOA->BSRR = GPIO_BSRR_BR12; // Encender segmento f
            GPIOC->BSRR = GPIO_BSRR_BR6;  // Encender segmento g
            break;
        case 9:
            GPIOB->BSRR = GPIO_BSRR_BR12; // Encender segmento a
            GPIOC->BSRR = GPIO_BSRR_BR5;  // Encender segmento b
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            GPIOC->BSRR = GPIO_BSRR_BR9;  // Encender segmento d
            GPIOA->BSRR = GPIO_BSRR_BR12; // Encender segmento f
            GPIOC->BSRR = GPIO_BSRR_BR6;  // Encender segmento g
            break;
        default:
            break;
    }
}

static void fotocompuerta_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN; // Habilitar reloj para GPIOB

    GPIOB->MODER &= ~GPIO_MODER_MODE13; // Configurar PB13 como entrada
    GPIOB->MODER &= ~GPIO_MODER_MODE14; // Configurar PB14 como entrada

    GPIOB->PUPDR &= ~GPIO_PUPDR_PUPD13; // No pull up- down para PB13
    GPIOB->PUPDR &= ~GPIO_PUPDR_PUPD14; // No pull up- down para PB14

    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN; // Habilitar reloj para el sistema de configuración

    SYSCFG->EXTICR[3] &= ~SYSCFG_EXTICR4_EXTI13; // Limpia la configuración de EXTI13
    SYSCFG->EXTICR[3] &= ~SYSCFG_EXTICR4_EXTI14; // Limpia la configuración de EXTI14

    SYSCFG->EXTICR[3] |= SYSCFG_EXTICR4_EXTI13_PB; // Configura EXTI13 para PB13
    SYSCFG->EXTICR[3] |= SYSCFG_EXTICR4_EXTI14_PB; // Configura EXTI14 para PB14

    EXTI->RTSR |= EXTI_RTSR_TR13; // Habilitar disparo por flanco de subida para EXTI13
    EXTI->FTSR &= ~EXTI_FTSR_TR13; // Deshabilitar disparo por flanco de bajada para EXTI13

    EXTI->RTSR &= ~EXTI_RTSR_TR14; // Deshabilitar disparo por flanco de subida para EXTI14
    EXTI->FTSR |= EXTI_FTSR_TR14; // Habilitar disparo por flanco de bajada para EXTI14

    EXTI->IMR |= EXTI_IMR_MR13 | EXTI_IMR_MR14; // Habilitar interrupción para EXTI13 y EXTI14

    EXTI->PR = EXTI_PR_PR13 | EXTI_PR_PR14; // Limpiar banderas de interrupción pendientes para EXTI13 y EXTI14

    NVIC_EnableIRQ(EXTI15_10_IRQn); // Matricular la interrupción EXTI15_10 en el NVIC
}

static void fotocompuerta_contador(void)
{
    if (fotocompuerta_U4_flag)
    {
        fotocompuerta_U4_flag = 0;
        if (secreto_good == 0) // Solo cuenta si no se esta mostrando "Good"
        {
            if (valor < 9999) // Establecer el limite para dar la vuelta entera
            {
                valor++;
            }
            else
            {
                valor = 0; // Reiniciar el valor cuando llega al máximo en 9999
            }
            secreto_evento(4); // FUNCION SECRETA: avisar que se activo U4
        }
    }

    if (fotocompuerta_U5_flag)
    {
        fotocompuerta_U5_flag = 0;
        if (secreto_good == 0) // Solo cuenta si no se esta mostrando "Good"
        {
            if (valor > 0) // Evitar que el valor sea negativo
            {
                valor--;
            }
            else
            {
                valor = 9999; // Dar la vuelta entera
            }
            secreto_evento(5); // avisar que se activo U5
        }
    }
}


/* Registra un evento de fotocompuerta. Si la OTRA se activo hace menos de ~200 ms, muestra "Good". */
static void secreto_evento(uint8_t fotocompuerta)
{
    if ((secreto_ventana > 0) && (secreto_ultima != 0) && (secreto_ultima != fotocompuerta))
    {
        secreto_good = SECRETO_GOOD_TICKS;      /* mostrar "Good"si el evento paso en menos de 200ms*/
        secreto_ultima = 0;
        secreto_ventana = 0;
    }
    else
    {
        secreto_ultima = fotocompuerta;         /* abrir la ventana para esta fotocompuerta */
        secreto_ventana = SECRETO_VENTANA_TICKS;
    }
}


static void secreto_tick(void)
{
    if (secreto_ventana > 0)
    {
        secreto_ventana--;
    }
    else
    {
        secreto_ultima = 0;
    }

    if (secreto_good > 0)
    {
        secreto_good--;
    }
}

static void display_seleccionar_letra(char letra)
{
    /*Apagar todos los segmentos*/
    GPIOB->BSRR = GPIO_BSRR_BS12; // Segmento a apagado
    GPIOC->BSRR = GPIO_BSRR_BS5 | GPIO_BSRR_BS8 | GPIO_BSRR_BS9 | GPIO_BSRR_BS6;  // Segmento b, c, d y g apagados
    GPIOA->BSRR = GPIO_BSRR_BS12 | GPIO_BSRR_BS11; // Segmento f y e apagados

    /*Encender los segmentos de la letra*/
    switch (letra)
    {
        case 'G':
            GPIOB->BSRR = GPIO_BSRR_BR12; // Encender segmento a
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            GPIOC->BSRR = GPIO_BSRR_BR9;  // Encender segmento d
            GPIOA->BSRR = GPIO_BSRR_BR11; // Encender segmento e
            GPIOA->BSRR = GPIO_BSRR_BR12; // Encender segmento f
            break;
        case 'o':
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            GPIOC->BSRR = GPIO_BSRR_BR9;  // Encender segmento d
            GPIOA->BSRR = GPIO_BSRR_BR11; // Encender segmento e
            GPIOC->BSRR = GPIO_BSRR_BR6;  // Encender segmento g
            break;
        case 'd':
            GPIOC->BSRR = GPIO_BSRR_BR5;  // Encender segmento b
            GPIOC->BSRR = GPIO_BSRR_BR8;  // Encender segmento c
            GPIOC->BSRR = GPIO_BSRR_BR9;  // Encender segmento d
            GPIOA->BSRR = GPIO_BSRR_BR11; // Encender segmento e
            GPIOC->BSRR = GPIO_BSRR_BR6;  // Encender segmento g
            break;
        default:
            break;
    }
}
