/******************************************************************************
 * Copyright 2013-2014 Espressif Systems (Wuxi)
 *
 * FileName: uart.c
 *
 * Description: Two UART mode configration and interrupt handler.
 *              Check your hardware connection while use this mode.
 *
 * Modification history:
 *     2014/3/12, v1.0 create this file.
 *
 *     2017/2/14	Added serial port reception with instant feedback, not only when the rx buffer is full
 *
 *
*******************************************************************************/
#include "ets_sys.h"
#include "osapi.h"
#include "driver/uart.h"
#include "osapi.h"
#include "driver/uart_register.h"
#include "mem.h"
// UartDev is defined and initialized in rom code.
extern UartDevice    UartDev;

struct Rx_t{
	char 	buf[RX_FIFO_SIZE];
	char	*in_pnt;			//used by the application call
	char	*out_pnt;			//used by the application call
	char	*in_pnt_int;		// used by the interrupt internally
	char	*out_pnt_int;		// used by the interrupt internally
// now for the atomic access semaphores
volatile uint8_t	lock;		// telling the interrupt this the code is in a critical phase, don't adjust pointers
volatile uint8_t	reset_lock; // this is used during a comm port reset, no changes are allowed anywhere until the changes are made.
} RxFiFo;



LOCAL void uart0_rx_intr_handler(void *para);

LOCAL void ICACHE_FLASH_ATTR
uart_config(uint8 uart_no)
{
  if (uart_no == UART1)
  {
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_U1TXD_BK);
  }
  else
  {
    /* rcv_buff size if 0x100 */
    ETS_UART_INTR_ATTACH(uart0_rx_intr_handler,  &(UartDev.rcv_buff));
    PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);
//todo ?    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_U0RTS);
  }

  uart_div_modify(uart_no, UART_CLK_FREQ / (UartDev.baut_rate));

  WRITE_PERI_REG(UART_CONF0(uart_no), UartDev.exist_parity
                 | UartDev.parity
                 | (UartDev.stop_bits << UART_STOP_BIT_NUM_S)
                 | (UartDev.data_bits << UART_BIT_NUM_S));

  //clear rx and tx fifo,not ready
  SET_PERI_REG_MASK(UART_CONF0(uart_no), UART_RXFIFO_RST | UART_TXFIFO_RST);
  CLEAR_PERI_REG_MASK(UART_CONF0(uart_no), UART_RXFIFO_RST | UART_TXFIFO_RST);


  if (uart_no == UART0)
  {
    //set rx fifo trigger
    WRITE_PERI_REG(UART_CONF1(uart_no),
                   ((0x7F & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S) |
                   //((128 & UART_RX_FLOW_THRHD) << UART_RX_FLOW_THRHD_S) |
                   //UART_RX_FLOW_EN |
                   ((0x0F & UART_TXFIFO_EMPTY_THRHD) << UART_TXFIFO_EMPTY_THRHD_S));

    //UART_RX_TOUT_EN);

    SET_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_TOUT_INT_ENA | UART_FRM_ERR_INT_ENA);
  }
  else
  {
    WRITE_PERI_REG(UART_CONF1(uart_no),
                   ((UartDev.rcv_buff.TrigLvl & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S));
  }

  //clear all interrupt
  WRITE_PERI_REG(UART_INT_CLR(uart_no), 0xffff);
  //enable rx_interrupt
  SET_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_FULL_INT_ENA | UART_RXFIFO_OVF_INT_ENA);
}

LOCAL STATUS
uart_tx_one_char(uint8 uart, uint8 TxChar)
{
  while (true)
  {
    uint32 fifo_cnt = READ_PERI_REG(UART_STATUS(uart)) & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);
    if ((fifo_cnt >> UART_TXFIFO_CNT_S & UART_TXFIFO_CNT) < 126) {
      break;
    }
  }
  WRITE_PERI_REG(UART_FIFO(uart) , TxChar);
  return OK;
}

void ICACHE_FLASH_ATTR
uart1_write_char(char c)
{
  if (c == '\n')
  {
    uart_tx_one_char(UART1, '\r');
    uart_tx_one_char(UART1, '\n');
  }
  else if (c == '\r')
  {
  }
  else
  {
    uart_tx_one_char(UART1, c);
  }
}

void ICACHE_FLASH_ATTR
uart0_write_char(char c)
{
  if (c == '\n')
  {
    uart_tx_one_char(UART0, '\r');
    uart_tx_one_char(UART0, '\n');
  }
  else if (c == '\r')
  {
  }
  else
  {
    uart_tx_one_char(UART0, c);
  }
}

void ICACHE_FLASH_ATTR
uart0_tx_buffer(uint8 *buf, uint16 len)
{
  uint16 i;

  for (i = 0; i < len; i++)
  {
    uart_tx_one_char(UART0, buf[i]);
  }
}

void ICACHE_FLASH_ATTR
uart0_tx_buffer_nz(char *c_pnt)
{
  uint16 i;

  while (*c_pnt)
  {
    uart_tx_one_char(UART0, *c_pnt);
    c_pnt++;
  }
}

void ICACHE_FLASH_ATTR
uart0_sendStr(const char *str)
{
  while (*str)
  {
    uart_tx_one_char(UART0, *str++);
  }
}


// extract data from the HW fifo to the sw fifo
LOCAL void
uart0_rx_data_hw_fifo_to_sw_fifo(int interrupt){
	static	uint8_t	busy;
	static uint8_t	interrupt_has_occured = 0;
	uint8 RcvChar;

	char	*in_pnt;
	char	*out_pnt;
	char	*next_pnt;

	if (interrupt) 	interrupt_has_occured = 1;
	// this exists in case an application level extraction is interrupted by an interrupt level call
	// under interrupt level calls, at least some bytes need to be cleared to make space for the incoming stream
	// data loss will occur, possible space for future error handling


	if (busy) return;
	busy = 1;
	// don't double enter this routine


// dump any data
	if (RxFiFo.reset_lock){
		while ((READ_PERI_REG(UART_STATUS(UART0)) >> UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT)
		{
			RcvChar = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
		}
	}



    if (RxFiFo.reset_lock == 0){

        if (RxFiFo.lock == 0){
        	RxFiFo.out_pnt_int = RxFiFo.out_pnt;
        }

        out_pnt = RxFiFo.out_pnt_int;
        in_pnt = RxFiFo.in_pnt_int;

// bounding
        if (in_pnt < RxFiFo.buf) in_pnt = RxFiFo.buf;
        if (in_pnt > (RxFiFo.buf + RX_FIFO_SIZE -1)) in_pnt = RxFiFo.buf;

        if (out_pnt < RxFiFo.buf) out_pnt = RxFiFo.buf;
        if (out_pnt > (RxFiFo.buf + RX_FIFO_SIZE -1)) out_pnt = RxFiFo.buf;

		next_pnt = in_pnt + 1;
		if (next_pnt > (RxFiFo.buf + RX_FIFO_SIZE -1)) next_pnt = RxFiFo.buf;


	// here we orderly only extract as much data as we have space to do
		while (((READ_PERI_REG(UART_STATUS(UART0)) >> UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT) && (next_pnt != out_pnt))
		{
		//TODO: MCU_Input( READ_PERI_REG(UART_FIFO(UART0)) & 0xFF );

	// any interrupt that has happened before here will be taken care of here, where at least on byte is extracted.
			interrupt_has_occured = 0;
	// extract the byte
			RcvChar = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;


	// see if there is space available
			next_pnt = in_pnt + 1;
			if (next_pnt > (RxFiFo.buf + RX_FIFO_SIZE -1))
				next_pnt = RxFiFo.buf;
			else{
// this is for when the buffer is full. error handling anyone?
			}

			// save the data
			if (next_pnt != out_pnt)
			{
				*in_pnt = RcvChar;
				in_pnt = next_pnt;
			}
		}


		// update pointers
		RxFiFo.in_pnt_int = in_pnt;
		RxFiFo.out_pnt_int = out_pnt;

		if (RxFiFo.lock == 0){
			RxFiFo.in_pnt = RxFiFo.in_pnt_int;
		}
	}



    if (interrupt_has_occured){
// just pull all remainding data from the que,
// there is so space to store it
// the application will have to deal with the lost data
        while ((READ_PERI_REG(UART_STATUS(UART0)) >> UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT)
        {
            RcvChar = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
        }
    }

	busy = 0;
}




LOCAL void
uart0_rx_intr_handler(void *para)
{
    uint8 RcvChar;
    RcvMsgBuff *pRxBuff = (RcvMsgBuff *)para;
	char	*next_pnt;

  uint8 uart_no = UART0;//UartDev.buff_uart_no;
  /* Is the frame Error interrupt set ? */
  if (UART_FRM_ERR_INT_ST == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_FRM_ERR_INT_ST))
  {
    //INFO("FRM_ERR\r\n");
    WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_FRM_ERR_INT_CLR);
  }
  else if (UART_RXFIFO_FULL_INT_ST == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_RXFIFO_FULL_INT_ST)) /*fifo full*/
  {
    CLEAR_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_FULL_INT_ENA | UART_RXFIFO_TOUT_INT_ENA);
    WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);
    //INFO("Fifo full: %d\n", (READ_PERI_REG(UART_STATUS(UART0))>>UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT);

    uart0_rx_data_hw_fifo_to_sw_fifo(1);

    WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);

    SET_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_FULL_INT_ENA | UART_RXFIFO_TOUT_INT_ENA);

  }
  else if (UART_RXFIFO_TOUT_INT_ST == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_RXFIFO_TOUT_INT_ST))
  {

    CLEAR_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_FULL_INT_ENA | UART_RXFIFO_TOUT_INT_ENA);
    WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_TOUT_INT_CLR);
    //INFO("Fifo timeout: %d\n", (READ_PERI_REG(UART_STATUS(UART0))>>UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT);
    while ((READ_PERI_REG(UART_STATUS(UART0)) >> UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT)
    {
      //MCU_Input( READ_PERI_REG(UART_FIFO(UART0)) & 0xFF );
    }
    SET_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_FULL_INT_ENA | UART_RXFIFO_TOUT_INT_ENA);

  }
  else if (UART_RXFIFO_OVF_INT_ST  == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_RXFIFO_OVF_INT_ST))
  {
    //INFO("FIFO FULL\n");
    WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_RXFIFO_OVF_INT_CLR);
  }
  else if (UART_TXFIFO_EMPTY_INT_ST == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_TXFIFO_EMPTY_INT_ST)) {
    //INFO("TX EMPTY\n");
    WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_TXFIFO_EMPTY_INT_CLR);
    CLEAR_PERI_REG_MASK(UART_INT_ENA(UART0), UART_TXFIFO_EMPTY_INT_ENA);
  }

  ETS_UART_INTR_ENABLE();
}

void ICACHE_FLASH_ATTR
uart_init(UartBautRate uart0_br, UartBautRate uart1_br)
{

	UartDev.baut_rate = uart0_br;
	uart_config(UART0);
	UartDev.baut_rate = uart1_br;
	uart_config(UART1);
	ETS_UART_INTR_ENABLE();


// these hace to be done in these layer sequences
	RxFiFo.reset_lock = 1;
		RxFiFo.lock = 1;
			RxFiFo.in_pnt = RxFiFo.buf;
			RxFiFo.out_pnt = RxFiFo.buf;
			RxFiFo.in_pnt_int = RxFiFo.buf;
			RxFiFo.out_pnt_int = RxFiFo.buf;
		RxFiFo.lock = 0;
	RxFiFo.reset_lock = 0;

	// install uart1 putc callback
	os_install_putc1((void *)uart0_write_char);
}

void ICACHE_FLASH_ATTR
uart_reattach()
{
  uart_init(BIT_RATE_115200, BIT_RATE_115200);
}


// pnt points to where the incoming data to be stored.
// space tells the routine what the maximum amount of data to be written is.
// the subroutine returns the amount of bytes stored.
int ICACHE_FLASH_ATTR
uart0_rx_data(char	*pnt, uint32_t space) {

// check to extract data waiting in the HW fifo
	uart0_rx_data_hw_fifo_to_sw_fifo(0);

	uint32_t	leng_saved;
	leng_saved = 0;
	char	*in;
	char	*out;

	if (RxFiFo.reset_lock)
		return(0);

	RxFiFo.lock = 1;
		in = RxFiFo.in_pnt;
		out = RxFiFo.out_pnt;
	RxFiFo.lock = 0;


// bounding
    if (in < RxFiFo.buf) in = RxFiFo.buf;
    if (out < RxFiFo.buf) out = RxFiFo.buf;
    if (in > (RxFiFo.buf + RX_FIFO_SIZE -1)) in = RxFiFo.buf;
    if (out > (RxFiFo.buf + RX_FIFO_SIZE -1)) out = RxFiFo.buf;


// see if there is data available

    while ((out != in) && (space))
    {
//save
    	*pnt = *out;
// account
		space--;
		leng_saved++;
    	pnt++;
    	out++;
// bound
    	if (out > (RxFiFo.buf + RX_FIFO_SIZE -1)) out = RxFiFo.buf;
    }

    // update pointers
	RxFiFo.lock = 1;
		RxFiFo.out_pnt = out;
	RxFiFo.lock = 0;

	return(leng_saved);

}

