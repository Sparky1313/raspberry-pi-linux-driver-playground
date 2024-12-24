#ifndef CUSTOM_UART_DRIVER_H
#define CUSTOM_UART_DRIVER_H

typedef enum uart_data_bit_size_e
{
  UART_DATA_5_BITS,
  UART_DATA_6_BITS,
  UART_DATA_7_BITS,
  UART_DATA_8_BITS
} uart_data_bit_size_t;

typedef enum uart_parity_e
{
  UART_EVEN_PARITY,
  UART_ODD_PARITY,
  UART_NO_PARITY
} uart_parity_t;

typedef enum uart_stop_bits_e
{
  UART_STOP_BITS_1,
  UART_STOP_BITS_2
} uart_stop_bits_t;


int uart_init(uint32_t baud_rate, uart_data_bit_size_t data_bit_size, uart_parity_t parity, uart_stop_bits_t stop_bits);

#endif