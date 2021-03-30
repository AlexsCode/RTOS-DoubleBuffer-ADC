# RTOS-DoubleBuffer-ADC

RTOS Practise Code for Interview.

Using ESP_IDF port of freeRTOS Kernal.
The Code allows the user to request average ADC values through a serial task.
This reads and writes into a dual buffer setup,which allows for switching between buffers to allow read and write in threadsafe way.
Using Mutexs on critical sections and semaphores to signal when the queue is full and to be read.
