
#include <stdio.h>
#include <string.h>

#define DEBUG_FLAG 1 //debug message switch
//below value is for illustration, real value is embedded system dependent
#define BAUD_H 0x01
#define BAUD_L 0xC2

//************************************************************
//type define
typedef unsigned int   uint;
typedef unsigned char  uchar;

/* User defined ring buffer attributes */
struct ringbuffer_attribute
{
	uint n_elem;//sized to an power of 2
	uchar *buffer;
}attr;

struct ring_buffer
{
	uint n_elem;
	uchar *buf;
	volatile uint head; //head and tail are incremented and wrap around automatically when they overflow since they are unsigned int in C
	volatile uint tail; //head and tail are not wrapped within the bounds of the ring buffer
};

//************************************************************
//Embedded memory-mapped peripheral registers, declared volatile since its value could change unexpectedly.
//InterruptControlFlag mimic the interrupt registers for read/write control
volatile uchar ReadInterruptControlFlag=0;
volatile uchar WriteInterruptControlFlag=0;
//mimic UART in and out register for read/write
volatile uchar UARTIN=0;
volatile uchar UARTOUT=0;

//suppose we have other registersm such as baud rate/bps rate.
volatile uchar BAUD0 = 0;
volatile uchar BAUD1 = 0;

volatile uchar * UART_READ_ADDR = &UARTIN;//memory mapped read address
volatile uchar * UART_WRITE_ADDR = &UARTOUT;//memory mapped write address

//ring buffer
#define RING_BUFFER_SIZE 8
static uchar RingBufferMemory[RING_BUFFER_SIZE];

//below static global variable and function are only visible in this file
static struct ring_buffer ringbuffer;
static int ring_buffer_full(struct ring_buffer *rbp);
static int ring_buffer_empty(struct ring_buffer *rbp);

//************************************************************
//functions
//************************************************************
//************************************************************
//Check whether the ring buffer is full
//param[in] rbp - pointer to the ring buffer
//return 1 if full, 0 otherwise 
//************************************************************
static int ring_buffer_full(struct ring_buffer *rbp)
{
	return ((rbp->head - rbp->tail) == rbp->n_elem) ? 1 : 0;
}

//************************************************************
//Check whether the ring buffer is empty
//param[in] rbp - pointer to the ring buffer
//return 1 if empty, 0 otherwise 
//************************************************************
static int ring_buffer_empty(struct ring_buffer *rbp)
{
	return ((rbp->head - rbp->tail) == 0U) ? 1 : 0;
}

//************************************************************
//Initialize the global ring buffer
//param[in] attr - ring buffer attributes
//return 0 on success, -1 otherwise 
//************************************************************
int ring_buffer_init(struct ringbuffer_attribute *attr)
{
	int err = -1;

	if ( (attr != NULL)) 
	{
		if ((attr->buffer != NULL)) 
		{
			/* Check that the size of the ring buffer is a power of 2 */
			if (((attr->n_elem - 1) & attr->n_elem) == 0) 
			{
				/* Initialize the ring buffer internal variables */
				ringbuffer.head = 0;
				ringbuffer.tail = 0;
				ringbuffer.buf = attr->buffer;
				ringbuffer.n_elem = attr->n_elem;
				err= 0;
			} 
		}
	}

	return err;
}

//************************************************************
//Add an element to the ring buffer
//param[in] data - the data to add
//return 0 on success, -1 otherwise 
//************************************************************
int ring_buffer_put(volatile uchar *data)
{
	int err = -1;
	if ((ring_buffer_full(&ringbuffer) == 0)) //ring buffer not full
	{
		//must be wrapped around the number of elements in the ring buffer to obtain which element we want to write to
		const uint offset = (ringbuffer.head & (ringbuffer.n_elem - 1)) ;
		memcpy(&(ringbuffer.buf[offset]), data, sizeof(*data));
		ringbuffer.head++;		
		err = 0;
		if(DEBUG_FLAG)printf("ring_buffer_put(): *data = %c, ringbuffer.buf offset=%d, head=%d\n", *data, offset, ringbuffer.head);
	} 
	return err;
}

//************************************************************
//Get (and remove) an element from the ring buffer
//param[in] data - pointer to store the data
//return 0 on success, -1 otherwise 
//************************************************************
int ring_buffer_get(char *data)
{
	int err = -1;

	if (ring_buffer_empty(&ringbuffer) == 0) //ring buffer not empty
	{
		const uint offset = (ringbuffer.tail & (ringbuffer.n_elem - 1)) ;
		memcpy(data, &(ringbuffer.buf[offset]), sizeof(*data));
		ringbuffer.tail++;				
		err = 0;
		if(DEBUG_FLAG)printf("ring_buffer_get(): *data = %c, ringbuffer.buf offset=%d, tail=%d\n", *data, offset, ringbuffer.tail);
	}
	return err;
}

//************************************************************
//an interrupt service routine which indicates that one byte has been received by a UART
//and interrupt has been triggered
//Read UART interrupt into a ring buffer can ensure minimum latency and interrupt safe.
//************************************************************
void UART_READ_interrupt(void)
{
	if (ReadInterruptControlFlag) //Interrupt come in
	{    
		ReadInterruptControlFlag = 0;// Clear the interrupt flag 
		ring_buffer_put(UART_READ_ADDR);		
	}
}

//******************************************************
// Read a character from UART's ring buffer
// return the character read on success, -1 if nothing was read
//******************************************************
int uart_getchar(void)
{
	int retval = -1;
	char c = -1;    
	if (ring_buffer_get(&c) == 0) {
		retval = (int) c;
	}
	return retval;
}

//******************************************************
//Write a character to UART
//param[in] c - the character to write
//return 0 on sucess, -1 otherwise
//******************************************************
int uart_putchar(int c)
{
	/* Wait for the transmit buffer to be ready */
	while (!WriteInterruptControlFlag);
	/* Transmit data */
	*UART_WRITE_ADDR = (char ) c; 
	WriteInterruptControlFlag = 0;// Clear the interrupt flag 
	return 0;
}

//******************************************************
//DEMO function
//******************************************************
#define demoNumber 4
int main(int argc, char *argv[])
{
	int demoCount=0;
	int value=0, i=0;

	// Initialize UART to 115200bps
	BAUD0 = BAUD_L;
	BAUD1 = BAUD_H;

	// Initialize the ring buffer
	attr.n_elem = RING_BUFFER_SIZE;
	attr.buffer = RingBufferMemory;
	if (ring_buffer_init(&attr) == 0)
	{
		//mimic UART interrupt and put the interrupt input into a ringbuffer
		UARTIN = 'A';
		for(i=0; i<demoNumber; i++)
		{
			UARTIN += 1;
			ReadInterruptControlFlag=1;//set interrupt register to trigger interrupt
			UART_READ_interrupt();
		}
	}

	//mimic UART read and write
	for(i=0; i<demoNumber; i++)
	{
		//READ UART
		value = uart_getchar();
		printf("value read from UART = %c\n", (char)value);
		value++;
		//WRITE UART
		WriteInterruptControlFlag=1;//turn on write interrupt
		uart_putchar(value);
		printf("after increase 1, value write to UART = %c\n", (char)value);
	}
	getchar();
	return 0;
}
