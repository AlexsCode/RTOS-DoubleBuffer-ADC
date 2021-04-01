/**
 * ESP RTOS Practise to prepare for Interviews
 * 
 * by ALEX TUDDENHAM
 * 30-03-2021
 * 
 */

//Specifies which of the 2 cores to use
#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

//fixed time delays and trigger words
static const char commandkey[] = "avg";          //char array with a-v-g preloaded.
static const uint16_t timer_div = 8;             //as 10 MHz timer.
static const uint64_t timer_max_count = 1000000; // max value
static const uint32_t cli_delay = 20;

//Fixed Sizes;
enum
{
  BUFFER_LEN = 10
}; //buffer size
enum
{
  MESSAGE_LEN = 100
}; // max message is 100 chars wide.
enum
{
  MESSAGE_QUEUE_LEN = 5
}; // MOST 5 messages in queue allowed.
enum
{
  COMMAND_BUFFER_LEN = 255
}; // number of characters in a command buffer.

//GPIO assignments
static const int adc_pin = A0;

//Structures and templates
typedef struct Message
{
  char body[MESSAGE_LEN]; // member of message is body including length
} Message;                // calling creation. // Acts as wrapper to stop one side acting on queue.

//Global Variables.
static hw_timer_t *timer = NULL;
static float adc_avg;

static volatile uint8_t BUFFER_OVERRUN = 0; // over run buffer flag,volatile as can change indirectly.

static volatile uint16_t buffer_primary[BUFFER_LEN];
static volatile uint16_t buffer_secondary[BUFFER_LEN]; // created two buffers of length.

static volatile uint16_t *write_ptr = buffer_primary;  //created a write pointer at prim on start
static volatile uint16_t *read_ptr = buffer_secondary; //created a read pointer at the sec buffer on start

//Queues and Semaphores
static QueueHandle_t message_queue; //queue and handler.
static TaskHandle_t processing_task = NULL;
static SemaphoreHandle_t sem_buffer_primary_full = NULL;
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

/**
 * Switches the pointers between buffers;
 */
void IRAM_ATTR swap_buffer()
{
  volatile uint16_t *new_pointer;
  new_pointer = write_ptr;
  write_ptr = read_ptr;
  read_ptr = new_pointer;
  //This will swap which buffer is being read from and which is being written to.
}

/**
 * ISR, ISR always top. Performs the read and flags when the buffer is full.
 */

void IRAM_ATTR onTimer()
{
  static uint16_t index = 0;       //index location
  BaseType_t task_woken = pdFALSE; //sets woken false as reset of interupt.

  if ((index < BUFFER_LEN) && (BUFFER_OVERRUN == 0))
  {
    write_ptr[index] = analogRead(adc_pin);
    index++; //stepping on the inex.
  }

  if (index >= BUFFER_LEN)
  { //overflow buffer primary.

    if (xSemaphoreTakeFromISR(sem_buffer_primary_full, &task_woken) == pdFALSE)
    { // woken should be false as set false at the higher level of loop so on the semaphore
      BUFFER_OVERRUN = 1;
    }

    if (BUFFER_OVERRUN == 0)
    {
      index = 0;
      swap_buffer(); //swaps between primary and secondary buffers
      vTaskNotifyGiveFromISR(processing_task, &task_woken);
    }

  }

  if (task_woken)
  {
    portYIELD_FROM_ISR();
  }
}

/**
 * Tasks CLI controls the serial interface, reading into.
 * 
 */

void CLITask(void *parameters)
{
  Message recieved_message;
  char c;
  char command_buffer[COMMAND_BUFFER_LEN];
  //created a message struct, a single char for indi and a char array for buffer;

  uint8_t index = 0;
  uint8_t command_len = strlen(commandkey); //length of a-v-g in this case.

  memset(command_buffer, 0, COMMAND_BUFFER_LEN); // should reset the whole buffer to 0;

  //inf loop here
  while (1)
  {
    if (xQueueReceive(message_queue, (void *)&recieved_message, 0) == pdTRUE)
    {
      Serial.println(recieved_message.body); //accessing message struct calling payload.
    }

    if (Serial.available() > 0)
    {
      c = Serial.read();

      if (index < (COMMAND_BUFFER_LEN - 1))
      {
        command_buffer[index] = c;
        index++;
      }

      if ((c == '\n') || (c == '\r'))
      {
        Serial.print("\r\n"); //CR

        command_buffer[index - 1] = '\0';
        if (strcmp(command_buffer, commandkey) == 0)
        {
          Serial.print("Average: ");
          Serial.println(adc_avg);
        }

        memset(command_buffer, 0, COMMAND_BUFFER_LEN);
        index = 0;
      }
      else
      {
        Serial.print(c);
      }
    }
    vTaskDelay(cli_delay / portTICK_PERIOD_MS);
  }
}

void calcAverage(void *parameters)
{
  Message msg;
  float avg;

  //start inf loop

  while (1)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    avg = 0.0; // setting float to 0 on each loop.
    for (int i = 0; i < BUFFER_LEN; i++)
    {
      avg += (float)read_ptr[i];
    }

    avg /= BUFFER_LEN; // average of the total size.

    portENTER_CRITICAL(&spinlock);
    adc_avg = avg;
    portEXIT_CRITICAL(&spinlock);
    //exited critical section after setting average.

    //if overrunning do this.

    if (BUFFER_OVERRUN == 1)
    {
      strcpy(msg.body, "error overrun");
      xQueueSend(message_queue, (void *)&msg, 10);
    }

    //end of reading
    portENTER_CRITICAL(&spinlock);
    BUFFER_OVERRUN = 0;                      //reset flag
    xSemaphoreGive(sem_buffer_primary_full); //no longer full
    portEXIT_CRITICAL(&spinlock);
  }
}

void setup()
{
  Serial.begin(115200);                  //esp serial rate
  vTaskDelay(1000 / portTICK_PERIOD_MS); //blocking 1 sec //one shot

  Serial.println();
  Serial.println("FREERTOS ADC - ALEX");

  sem_buffer_primary_full = xSemaphoreCreateBinary();

  if (sem_buffer_primary_full == NULL)
  {
    Serial.println("Error - could not build semaphore");
    ESP.restart();
  }
  xSemaphoreGive(sem_buffer_primary_full); //given value 1 to start with;

  message_queue = xQueueCreate(MESSAGE_QUEUE_LEN, sizeof(Message));

  xTaskCreatePinnedToCore(CLITask, "CLI", 1024, NULL, 2, NULL, app_cpu);

  xTaskCreatePinnedToCore(calcAverage, "Calculate Avg", 1024, NULL, 1, &processing_task, app_cpu);

  //task CLI highest priority but has a time limitation, Calc avg is idletask effectively.

  timer = timerBegin(0, timer_div, true);
  timerAttachInterrupt(timer, &onTimer, true);

  timerAlarmWrite(timer, timer_max_count, true);

  timerAlarmEnable(timer);

  vTaskDelete(NULL); //shouldnt delete key tasks , just arduino IDE based.
}

void loop()
{
  //not used Arduino Required. 
}
