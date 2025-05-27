#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "lib/ssd1306.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "pico/bootrom.h"
#include "stdio.h"
#include "hardware/pwm.h"

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define ENDERECO 0x3C

// Definindo as dimensões do display
#define WIDTH 128
#define HEIGHT 64

#define BOTAO_A 5 // Entrada
#define BOTAO_B 6 // Saída
#define BOTAO_J 22  // Reset

// LEDs RGB e Buzzer
#define LED_G   11
#define LED_B   12
#define LED_R   13
#define BUZZER  21

// Funções para o Buzzer
void init_pwm(uint gpio) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv(slice_num, 125.0f);
    pwm_set_wrap(slice_num, 1000);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), 0);
    pwm_set_enabled(slice_num, true);
}
void set_buzzer_tone(uint gpio, uint freq) {
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    uint top = 1000000 / freq;
    pwm_set_wrap(slice_num, top);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), top / 2);
}
void stop_buzzer(uint gpio) {
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), 0);
}

// Variáveis globais
absolute_time_t last_interrupt_time = 0;
ssd1306_t ssd;
SemaphoreHandle_t xContadorSem;      // Semáforo de contagem para incremento
SemaphoreHandle_t xDecrementoSem;    // Semáforo binário para decremento
SemaphoreHandle_t xResetSem;         // Semáforo binário para reset
SemaphoreHandle_t xContadorMutex;    // Mutex para proteger eventosProcessados
uint16_t eventosProcessados = 0;     // Contador de Usuários do Sistema
bool semVagas = false;               // Para o Beep de Sem vagas 
bool resetVagas = false;             // Para o Beep de Reset
int MAX = 10;                        // Quantidade de vagas

// ISRs dos botões
void gpio_entrada(uint gpio, uint32_t events)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xContadorSem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
void gpio_saida(uint gpio, uint32_t events)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xDecrementoSem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
void gpio_reset(uint gpio, uint32_t events)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xResetSem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Task para incrementar contador
void vTaskEntrada(void *params) {
    char buffer[64];
    
    while (true)
    {
        // Aguarda semáforo de contagem
        if (xSemaphoreTake(xContadorSem, portMAX_DELAY) == pdTRUE)
        {
            // Protege o acesso com mutex
            if (xSemaphoreTake(xContadorMutex, portMAX_DELAY) == pdTRUE)
            {
                if(eventosProcessados < 10){
                    eventosProcessados++;

                    // Atualiza display
                    ssd1306_fill(&ssd, 0);
                    ssd1306_draw_string(&ssd, "Usuario", 40, 10);
                    ssd1306_draw_string(&ssd, "recebido!", 30, 19);
                    sprintf(buffer, "Usuarios: %d", eventosProcessados);
                    ssd1306_draw_string(&ssd, buffer, 5, 44);
                    sprintf(buffer, "Vagas: %d", MAX-eventosProcessados);
                    ssd1306_draw_string(&ssd, buffer, 5, 54);
                    ssd1306_send_data(&ssd);

                } else {
                    // Atualiza display
                    ssd1306_fill(&ssd, 0);
                    ssd1306_draw_string(&ssd, "Nao ha", 40, 10);
                    ssd1306_draw_string(&ssd, "Mais Vagas!", 30, 19);
                    sprintf(buffer, "Usuarios: %d", eventosProcessados);
                    ssd1306_draw_string(&ssd, buffer, 5, 44);
                    sprintf(buffer, "Vagas: %d", MAX-eventosProcessados);
                    ssd1306_draw_string(&ssd, buffer, 5, 54);
                    ssd1306_send_data(&ssd);
                    semVagas = true;
                }
                
                // Libera o mutex
                xSemaphoreGive(xContadorMutex);
                
                // Delay fora da seção crítica
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
    }
}

// Task para decrementar contador
void vTaskSaida(void *params) {
    char buffer[64];
    
    while(1) {
        // Aguarda semáforo binário
        if (xSemaphoreTake(xDecrementoSem, portMAX_DELAY) == pdTRUE)
        {
            // Protege o acesso com mutex
            if (xSemaphoreTake(xContadorMutex, portMAX_DELAY) == pdTRUE)
            {
                if (eventosProcessados > 0) {
                    eventosProcessados--;
                    
                    ssd1306_fill(&ssd, 0);
                    ssd1306_draw_string(&ssd, "Usuario", 40, 10);
                    ssd1306_draw_string(&ssd, "removido!", 30, 19);
                    sprintf(buffer, "Usuarios: %d", eventosProcessados);
                    ssd1306_draw_string(&ssd, buffer, 5, 44);
                    sprintf(buffer, "Vagas: %d", MAX-eventosProcessados);
                    ssd1306_draw_string(&ssd, buffer, 5, 54);
                    ssd1306_send_data(&ssd);
                    
                } else {
                    ssd1306_fill(&ssd, 0);
                    ssd1306_draw_string(&ssd, "Nenhum usuario", 10, 15);
                    ssd1306_draw_string(&ssd, "para remover!", 15, 25);
                    sprintf(buffer, "Usuarios: %d", eventosProcessados);
                    ssd1306_draw_string(&ssd, buffer, 5, 44);
                    sprintf(buffer, "Vagas: %d", MAX-eventosProcessados);
                    ssd1306_draw_string(&ssd, buffer, 5, 54);
                    ssd1306_send_data(&ssd);
                }
                
                // Libera o mutex
                xSemaphoreGive(xContadorMutex);
                
                // Delay fora da seção crítica
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
    }
}

// Task para resetar contador
void vTaskReset(void *params) {
    char buffer[64];    
    
    while(1) {
        // Aguarda semáforo binário para reset
        if (xSemaphoreTake(xResetSem, portMAX_DELAY) == pdTRUE)
        {
            // Protege o acesso com mutex
            if (xSemaphoreTake(xContadorMutex, portMAX_DELAY) == pdTRUE)
            {
                uint16_t valorAnterior = eventosProcessados;
                eventosProcessados = 0;  // Reset do contador
                
                // Mostra reset no display
                ssd1306_fill(&ssd, 0);
                ssd1306_draw_string(&ssd, "RESET!", 45, 30);
                ssd1306_send_data(&ssd);
                resetVagas = true;
                
                // Libera o mutex
                xSemaphoreGive(xContadorMutex);
                
                // Mostra a mensagem por mais tempo
                vTaskDelay(pdMS_TO_TICKS(1500));
                
                // Volta para tela de aguardando
                if (xSemaphoreTake(xContadorMutex, portMAX_DELAY) == pdTRUE)
                {
                    ssd1306_fill(&ssd, 0);
                    ssd1306_draw_string(&ssd, "Sistema", 35, 10);
                    ssd1306_draw_string(&ssd, "Resetado!", 30, 20);
                    ssd1306_draw_string(&ssd, "Aguardando", 25, 35);
                    ssd1306_draw_string(&ssd, "Usuarios...", 30, 45);
                    ssd1306_send_data(&ssd);
                    
                    xSemaphoreGive(xContadorMutex);
                }
            }
        }
    }
}

// Task do LED RGB
void vTaskLED(void *params) {
    char buffer[64];
    while (true){
        if (eventosProcessados == 0) {
            gpio_put(LED_R, 0);
            gpio_put(LED_G, 0);
            gpio_put(LED_B, 1);
            vTaskDelay(pdMS_TO_TICKS(100));

        } else if (eventosProcessados <= MAX-2) {
            gpio_put(LED_R, 0);
            gpio_put(LED_G, 1);
            gpio_put(LED_B, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            
        } else if (eventosProcessados == MAX-1) {
            gpio_put(LED_R, 1);
            gpio_put(LED_G, 1);
            gpio_put(LED_B, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            
        } else if (eventosProcessados == MAX) {
            gpio_put(LED_R, 1);
            gpio_put(LED_G, 0);
            gpio_put(LED_B, 0);
            vTaskDelay(pdMS_TO_TICKS(100));            
        }
    }
}

// Task do Buzzer
void vTaskBuzzer (void *params){
    char buffer[64];
    while (true){
        if (semVagas == true) {
            set_buzzer_tone(BUZZER, 330);
            vTaskDelay(pdMS_TO_TICKS(150));
            stop_buzzer(BUZZER);
            semVagas = false;

        } else if (resetVagas == true) {
            set_buzzer_tone(BUZZER, 440);
            vTaskDelay(pdMS_TO_TICKS(100));
            stop_buzzer(BUZZER);
            vTaskDelay(pdMS_TO_TICKS(100));
            set_buzzer_tone(BUZZER, 440);
            vTaskDelay(pdMS_TO_TICKS(100));
            stop_buzzer(BUZZER);
            resetVagas = false;            
        }
    }
}

// Handler principal das interrupções GPIO
void gpio_irq_handler(uint gpio, uint32_t events)
{
    absolute_time_t now = get_absolute_time();
    int64_t diff = absolute_time_diff_us(last_interrupt_time, now);

    if (diff < 250000) return; // debounce de 250ms
    last_interrupt_time = now;

    switch(gpio) {
        case BOTAO_A:
            gpio_entrada(gpio, events);
            break;
        case BOTAO_B:
            gpio_saida(gpio, events);
            break;
        case BOTAO_J:
            gpio_reset(gpio, events);
            break;
    }
}

int main()
{
    stdio_init_all();

    // Iniciando LEDs e Buzzer
    init_pwm(BUZZER);
    gpio_init(LED_R);
    gpio_init(LED_G);
    gpio_init(LED_B);
    gpio_set_dir(LED_R, GPIO_OUT);
    gpio_set_dir(LED_G, GPIO_OUT);
    gpio_set_dir(LED_B, GPIO_OUT);
    gpio_put(LED_R, 0);
    gpio_put(LED_G, 0);
    gpio_put(LED_B, 0);
    
    // Inicialização do I2C
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicialização do display
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO, I2C_PORT);
    ssd1306_config(&ssd);
    
    // Configura os botões
    gpio_init(BOTAO_A);
    gpio_init(BOTAO_B);
    gpio_init(BOTAO_J);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_set_dir(BOTAO_J, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_pull_up(BOTAO_B);
    gpio_pull_up(BOTAO_J);

    // Configura interrupções
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BOTAO_J, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Cria todos os semáforos e mutex
    xContadorSem = xSemaphoreCreateCounting(10, 0);
    xDecrementoSem = xSemaphoreCreateCounting(10, 0);
    xResetSem = xSemaphoreCreateBinary();
    xContadorMutex = xSemaphoreCreateMutex();

    // Mostra mensagem inicial
    ssd1306_fill(&ssd, 0);
    ssd1306_draw_string(&ssd, "Sistema", 35, 5);
    ssd1306_draw_string(&ssd, "Iniciado!", 30, 15);
    ssd1306_draw_string(&ssd, "A = Entrada", 10, 35);
    ssd1306_draw_string(&ssd, "B = Saida", 10, 45);
    ssd1306_draw_string(&ssd, "J = Reset",10, 55);
    ssd1306_send_data(&ssd);

    // Cria as tarefas
    xTaskCreate(vTaskEntrada, "Entrada", configMINIMAL_STACK_SIZE + 256, NULL, 2, NULL);
    xTaskCreate(vTaskSaida, "Saida", configMINIMAL_STACK_SIZE + 256, NULL, 2, NULL);
    xTaskCreate(vTaskReset, "Reset", configMINIMAL_STACK_SIZE + 256, NULL, 3, NULL);
    xTaskCreate(vTaskLED, "LED RGB", configMINIMAL_STACK_SIZE + 256, NULL, 2, NULL);
    xTaskCreate(vTaskBuzzer, "Buzzer", configMINIMAL_STACK_SIZE + 256, NULL, 2, NULL);

    // Inicia o scheduler
    vTaskStartScheduler();
    panic_unsupported();
}