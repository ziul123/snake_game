#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"

// Biblioteca gerada pelo arquivo .pio durante compilação.
#include "ws2818b.pio.h"

#pragma region LEDS
// Definição do número de LEDs e pino.
#define LED_COUNT 25
#define LED_PIN 7

// Definição de pixel GRB
struct pixel_t {
  uint8_t G, R, B; // Três valores de 8-bits compõem um pixel.
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t; // Mudança de nome de "struct pixel_t" para "npLED_t" por clareza.

// Declaração do buffer de pixels que formam a matriz.
npLED_t leds[LED_COUNT];

// Variáveis para uso da máquina PIO.
PIO np_pio;
uint sm;

/**
 * Inicializa a máquina PIO para controle da matriz de LEDs.
 */
void npInit(uint pin) {

  // Cria programa PIO.
  uint offset = pio_add_program(pio0, &ws2818b_program);
  np_pio = pio0;

  // Toma posse de uma máquina PIO.
  sm = pio_claim_unused_sm(np_pio, false);
  if (sm < 0) {
    np_pio = pio1;
    sm = pio_claim_unused_sm(np_pio, true); // Se nenhuma máquina estiver livre, panic!
  }

  // Inicia programa na máquina PIO obtida.
  ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);

  // Limpa buffer de pixels.
  for (uint i = 0; i < LED_COUNT; ++i) {
    leds[i].R = 0;
    leds[i].G = 0;
    leds[i].B = 0;
  }
}

/**
 * Atribui uma cor RGB a um LED.
 */
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
  leds[index].R = r;
  leds[index].G = g;
  leds[index].B = b;
}

/**
 * Limpa o buffer de pixels.
 */
void npClear() {
  for (uint i = 0; i < LED_COUNT; ++i)
    npSetLED(i, 0, 0, 0);
}

/**
 * Escreve os dados do buffer nos LEDs.
 */
void npWrite() {
  // Escreve cada dado de 8-bits dos pixels em sequência no buffer da máquina PIO.
  for (uint i = 0; i < LED_COUNT; ++i) {
    pio_sm_put_blocking(np_pio, sm, leds[i].G);
    pio_sm_put_blocking(np_pio, sm, leds[i].R);
    pio_sm_put_blocking(np_pio, sm, leds[i].B);
  }
  sleep_us(100); // Espera 100us, sinal de RESET do datasheet.
}

#pragma endregion

#pragma region GAME_LOGIC

#define NUM_ROWS 5
#define NUM_COLS 5
#define BOARD_SIZE 25

typedef enum GAME_OBJS {EPT, SNK, FRT} game_obj_t;
typedef enum BOARD_DIRS {UP, DOWN, LEFT, RIGHT} board_dir_t;

game_obj_t game_board[NUM_ROWS][NUM_COLS] = {0};

typedef struct segment segment_t;

struct segment {
	uint8_t row; 
	uint8_t col; 
	segment_t *next;
	segment_t *prev;
};

segment_t snake[BOARD_SIZE] = {0};
segment_t *snake_head;
segment_t *snake_last;
segment_t *free_list;
uint8_t snake_size;
board_dir_t head_dir;

void game_init() {
  game_board[0][0] = SNK;
  snake[0] = (segment_t) {0, 0, NULL, NULL};
	snake_head = snake;
	snake_last = snake;
  snake_size = 1;
  head_dir = UP;
	for (uint8_t i = 1; i < BOARD_SIZE - 1; i++) {
		snake[i].next = &snake[i+1];
	}
	snake[snake_size - 1].next = NULL;
	free_list = &snake[1];
	game_board[0][1] = FRT;
	game_board[3][3] = FRT;
	game_board[3][4] = FRT;
	game_board[3][2] = FRT;
}

/* returns NULL if out of bounds */
game_obj_t *next_pos(segment_t *seg, board_dir_t dir) {
  switch (dir) {
    case UP:
      return seg->row >= NUM_ROWS - 1 ? NULL : &(game_board[++seg->row][seg->col]);
      break;
    case DOWN:
      return seg->row <= 0 ? NULL : &(game_board[--seg->row][seg->col]);
      break;
    case LEFT:
      return seg->col <= 0 ? NULL : &(game_board[seg->row][--seg->col]);
      break;
    case RIGHT:
      return seg->col >= NUM_COLS - 1 ? NULL : &(game_board[seg->row][++seg->col]);
      break;
  }
}

bool move_snake() {
	bool grow = false;
	segment_t h = *snake_head;
	game_obj_t *next = next_pos(&h, head_dir);
  if (!next || *next == SNK) return false;
  /* if it can move, check if it moves onto fruit */
  if (*next == FRT) grow = true;
	segment_t *f = free_list;
	free_list = f->next;
	*f = h;
	f->next = snake_head;
	snake_head->prev = f;
	snake_head = f;
	game_board[h.row][h.col] = SNK;
	
	if (grow) {
		snake_size++;
	} else {
		game_board[snake_last->row][snake_last->col] = EPT;
		segment_t *second_last = snake_last->prev;
		snake_last->next = free_list;
		free_list = snake_last;
		snake_last = second_last;
	}
	return true;
}

#pragma endregion

#pragma region JOYSTICK

#define VRY 26
#define VRX 27
#define ERROR_MARGIN 50

volatile board_dir_t last_dir;
repeating_timer_t joystick_timer;

bool joystick_timer_callback(__unused repeating_timer_t *) {
  adc_select_input(1);
  uint x = adc_read();
  adc_select_input(0);
  uint y = adc_read();
  if (y >= 4096 - ERROR_MARGIN) {
    last_dir = UP;
  } else if (y <= ERROR_MARGIN) {
    last_dir = DOWN;
  } else if (x >= 4096 - ERROR_MARGIN) {
    last_dir = RIGHT;
  } else if (x <= ERROR_MARGIN) {
    last_dir = LEFT;
  }
  return true;
}

void joystick_init() {
  adc_init();
  adc_gpio_init(26);
  adc_gpio_init(27);
  add_repeating_timer_ms(100, joystick_timer_callback, NULL, &joystick_timer);
}

#pragma endregion

#pragma region BUTTONS

#define BUTTON_A 5

void button_init() {
  gpio_init(BUTTON_A);
  gpio_set_dir(BUTTON_A, GPIO_IN);
  gpio_pull_up(BUTTON_A);
}

#pragma endregion

void set_leds() {
  npClear();
  for (uint8_t row = 0; row < NUM_ROWS; row++) {
    bool invert = row % 2 == 0;
    for (uint8_t col = 0; col < NUM_COLS; col++) {
      uint8_t led_i;
      if (!invert) {
        led_i = row * 5 + col;
      } else {
        led_i = row * 5 + (NUM_COLS - 1 - col);
      }
      switch (game_board[row][col]) {
      case SNK:
        leds[led_i].G = 100;
        break;
      case FRT:
        leds[led_i].R = 100;
        break;
      default:
        break;
      }
    }
  }
}

int main() {

  // Inicializa entradas e saídas.
  stdio_init_all();

  // Inicializa matriz de LEDs NeoPixel.
  npInit(LED_PIN);
  npClear();

  joystick_init();
  game_init();
  button_init();

  while (true) {
    set_leds();
    npWrite();
    head_dir = last_dir;
    if (!move_snake()) sleep_ms(UINT32_MAX);
    sleep_ms(500);
  }
}