#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#define WINDOW_SAMPLES 2048U
#define WINDOW_SHIFT   11

#define DC_INITIAL 256

typedef struct {
  uint32_t sum;
  uint32_t sum_sq;
  uint32_t sum_abs;
  uint32_t sum_diff2;
  uint16_t min;
  uint16_t max;
  uint16_t zc;
} acc_t;

static acc_t acc;
static acc_t ready;
static volatile uint8_t ready_flag = 0;
static volatile uint16_t n = 0;

static volatile uint16_t dc = DC_INITIAL;
static int16_t prev_dev = 0;
static uint16_t prev_x = 0;
static uint8_t first_sample = 1;

static void uart_init(void) {
  UCSR0A = (1 << U2X0);
  UBRR0H = 0;
  UBRR0L = 16;
  UCSR0B = (1 << TXEN0) | (1 << RXEN0);
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

static void uart_putc(char c) {
  while (!(UCSR0A & (1 << UDRE0))) { }
  UDR0 = c;
}

static void uart_puts(const char *s) {
  while (*s) uart_putc(*s++);
}

static void uart_put_u32(uint32_t v) {
  char buf[11];
  uint8_t i = 0;
  if (v == 0) { uart_putc('0'); return; }
  while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
  while (i > 0) uart_putc(buf[--i]);
}

static int16_t uart_getc_nonblock(void) {
  if (UCSR0A & (1 << RXC0)) return UDR0;
  return -1;
}

static void adc_init(void) {
  ADMUX = (1 << REFS0);
  DIDR0 = (1 << ADC0D);
  ADCSRB = 0;

  ADCSRA = (1 << ADEN) | (1 << ADATE) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
  ADCSRA |= (1 << ADSC);
}

static void timer2_init(void) {
  TCCR2A = (1 << WGM21);
  TCCR2B = (1 << CS21) | (1 << CS20);
  OCR2A = 124;
  TIMSK2 = (1 << OCIE2A);
}

static uint8_t fan_on = 0;
static uint8_t fan_duty = 200;

static void fan_init(void) {
  DDRD |= (1 << PD5) | (1 << PD6);
  PORTD &= ~(1 << PD6);

  TCCR0A = (1 << WGM01) | (1 << WGM00);
  TCCR0B = (1 << CS01) | (1 << CS00);
  OCR0B = 0;
}

static void fan_apply(void) {
  if (fan_on) {
    OCR0B = fan_duty;
    TCCR0A |= (1 << COM0B1);
  } else {
    TCCR0A &= ~(1 << COM0B1);
    PORTD &= ~(1 << PD5);
  }
}

static void leds_init(void) {
  DDRB |= (1 << PB0) | (1 << PB1);
  PORTB &= ~((1 << PB0) | (1 << PB1));
}

static void acc_reset(acc_t *a) {
  a->sum = 0; a->sum_sq = 0; a->sum_abs = 0; a->sum_diff2 = 0;
  a->min = 1023; a->max = 0; a->zc = 0;
}

ISR(TIMER2_COMPA_vect) {
  uint8_t lo = ADCL;
  uint8_t hi = ADCH;
  uint16_t x = ((uint16_t)hi << 8) | lo;
  ADCSRA |= (1 << ADSC);

  int16_t dev = (int16_t)x - (int16_t)dc;
  uint16_t adev = (dev < 0) ? (uint16_t)(-dev) : (uint16_t)dev;

  acc.sum += x;
  acc.sum_sq += (uint32_t)adev * adev;
  acc.sum_abs += adev;
  if (x < acc.min) acc.min = x;
  if (x > acc.max) acc.max = x;

  if (!first_sample) {
    int16_t d = (int16_t)x - (int16_t)prev_x;
    uint16_t ad = (d < 0) ? (uint16_t)(-d) : (uint16_t)d;
    acc.sum_diff2 += (uint32_t)ad * ad;

    if ((dev > 0 && prev_dev < 0) || (dev < 0 && prev_dev > 0)) acc.zc++;
  }
  first_sample = 0;
  if (dev != 0) prev_dev = dev;
  prev_x = x;

  if (++n >= WINDOW_SAMPLES) {
    ready = acc;
    ready_flag = 1;
    dc = (uint16_t)(acc.sum >> WINDOW_SHIFT);
    acc_reset(&acc);
    n = 0;
  }
}

static uint16_t isqrt32(uint32_t v) {
  uint32_t res = 0;
  uint32_t bit = 1UL << 30;
  while (bit > v) bit >>= 2;
  while (bit != 0) {
    if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; }
    else { res >>= 1; }
    bit >>= 2;
  }
  return (uint16_t)res;
}

int main(void) {
  uart_init();
  adc_init();
  timer2_init();
  fan_init();
  leds_init();
  acc_reset(&acc);
  sei();

  uart_puts("# acoustic_detector v1, fs=4000Hz, window=2048\r\n");
  uart_puts("window,mean,rms,p2p,crest_x100,zcr,mad,diff_rms,fan\r\n");

  uint32_t window_id = 0;
  acc_t w;

  for (;;) {
    int16_t c = uart_getc_nonblock();
    if (c == 'f') { fan_on = !fan_on; fan_apply(); }
    else if (c >= '1' && c <= '9') { fan_duty = (uint8_t)((c - '0') * 255U / 10U); fan_apply(); }
    else if (c == '0') { fan_duty = 255; fan_apply(); }
    else if (c == 'r') { PORTB ^= (1 << PB1); }

    if (!ready_flag) continue;

    cli();
    w = ready;
    ready_flag = 0;
    sei();

    uint16_t mean = (uint16_t)(w.sum >> WINDOW_SHIFT);
    uint16_t rms = isqrt32(w.sum_sq >> WINDOW_SHIFT);
    uint16_t p2p = w.max - w.min;

    uint16_t half_p2p = p2p / 2;
    uint16_t crest_x100 = rms ? (uint16_t)((uint32_t)half_p2p * 100U / rms) : 0;
    uint16_t mad = (uint16_t)(w.sum_abs >> WINDOW_SHIFT);
    uint16_t diff_rms = isqrt32(w.sum_diff2 >> WINDOW_SHIFT);

    uart_put_u32(window_id++); uart_putc(',');
    uart_put_u32(mean);        uart_putc(',');
    uart_put_u32(rms);         uart_putc(',');
    uart_put_u32(p2p);         uart_putc(',');
    uart_put_u32(crest_x100);  uart_putc(',');
    uart_put_u32(w.zc);        uart_putc(',');
    uart_put_u32(mad);         uart_putc(',');
    uart_put_u32(diff_rms);    uart_putc(',');
    uart_put_u32(fan_on);
    uart_puts("\r\n");

    PORTB ^= (1 << PB0);
  }
}
