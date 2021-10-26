#include <string.h>

#include <armdio.h>
#include <delayutil.h>
#include <font8x13.h>
#include <stm32f4xx.h>

static DIOTag_t lcd_rst = GPIOC_DIO(15);
static DIOTag_t lcd_cs = GPIOB_DIO(9);
static DIOTag_t lcd_a0 = GPIOC_DIO(14);
static DIOTag_t lcd_mosi = GPIOB_DIO(15); // AF05
static DIOTag_t lcd_sck = GPIOB_DIO(13);  // AF05
static SPI_TypeDef *lcd_spi = SPI2;

static uint8_t lcd_fbuf[160*128*3/2];

static inline void lcd_send_command(uint8_t cmd)
{
	delay_loop(100);
	DIOLow(lcd_a0);	// Low for command
	delay_loop(100);
	DIOLow(lcd_cs);
	delay_loop(100);
	lcd_spi->DR = cmd; // send SPI

	// and wait for completion
	while (!(lcd_spi->SR & SPI_SR_TXE));
	while ((lcd_spi->SR & SPI_SR_BSY));

	delay_loop(100);
	DIOHigh(lcd_cs);
}

void lcd_send_data(uint8_t data)
{
	delay_loop(100);
	DIOHigh(lcd_a0);
	delay_loop(100);
	DIOLow(lcd_cs);
	delay_loop(100);
	lcd_spi->DR = data; // send SPI

	// and wait for completion
	while (!(lcd_spi->SR & SPI_SR_TXE));
	while ((lcd_spi->SR & SPI_SR_BSY));
	delay_loop(100);
	DIOHigh(lcd_cs);
}

static inline void lcd_send_data_singlecolor(uint8_t r, uint8_t g, uint8_t b, int len)
{
	uint8_t bytes[] = {
		(r << 4) | g,
		(b << 4) | r,
		(g << 4) | b
	};

	delay_loop(100);
	DIOHigh(lcd_a0);
	delay_loop(100);
	DIOLow(lcd_cs);
	delay_loop(100);

	for (int i = 0; i < len; i++) {
		lcd_spi->DR = bytes[i % 3]; // send SPI

		// and wait for completion
		while (!(lcd_spi->SR & SPI_SR_TXE));
		while ((lcd_spi->SR & SPI_SR_BSY));
	}

	delay_loop(100);
	DIOHigh(lcd_cs);
}

static inline void lcd_send_data_bulk(uint8_t *data, int len)
{
	delay_loop(100);
	DIOHigh(lcd_a0);
	delay_loop(100);
	DIOLow(lcd_cs);
	delay_loop(100);
	for (int i = 0; i< len; i++) {
		lcd_spi->DR = data[i]; // send SPI

		// and wait for completion
		while (!(lcd_spi->SR & SPI_SR_TXE));
		while ((lcd_spi->SR & SPI_SR_BSY));
	}
	delay_loop(100);
	DIOHigh(lcd_cs);
}

static inline void lcd_blit_internal(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
	if (x < 0) return;
	if (y < 0) return;
	if (y > 127) return;
	if (x > 159) return;

	int addr = y * 160 + x;
	/* 1,0 = 1; 2,0 = 2   3,0 = 3 */
	addr *= 3;
	addr /= 2;
	/* 1 3   4 */

	if (x % 2) {
		lcd_fbuf[addr] = (lcd_fbuf[addr] & 0xf0) | r;
		lcd_fbuf[addr+1] = (g << 4) | b;
	} else {
		lcd_fbuf[addr] = (r << 4) | g;
		lcd_fbuf[addr+1] = (lcd_fbuf[addr+1] & 0x0f) | (b << 4);
	}
}

void lcd_blit(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
	lcd_blit_internal(x, y, r, g, b);
}

void lcd_blit_horiz(int x, int y, int x2, uint8_t r, uint8_t g, uint8_t b) {
	for (int i=x; i <= x2; i++) {
		lcd_blit_internal(i, y, r, g, b);
	}
}

void lcd_blit_rows(int y, int y2, uint8_t fr, uint8_t fg, uint8_t fb)
{
	for (int i=y; i<y2; i++) {
		lcd_blit_horiz(0, i, 159, fr, fg, fb);
	}
}

void lcd_move_up(int y, int y2, uint8_t fr, uint8_t fg, uint8_t fb)
{
	memmove(lcd_fbuf, lcd_fbuf+(y*160*3/2), (y2-y)*160*3/2);

	lcd_blit_rows(y2-y, y2, fr, fg, fb);
}

static void lcd_blit_char_internal(uint8_t c, int x, int y, uint8_t r, uint8_t g, uint8_t b,
		uint8_t bgr, uint8_t bgg, uint8_t bgb)
{
	if ((c < 0x20) || (c > 0x7e)) {
		c = ' ';
	}

	uint8_t (*raster)[13] = &font_8x13_rasters[c - 0x20];

	for (int i = 12 ; i >= 0; i--){
		uint8_t tmp = (*raster)[i];
		for (int j = 0 ; j < 9; j++) {
			if (tmp & 0x80) {
				lcd_blit_internal(x+j, y, r, g, b);
			} else {
				lcd_blit_internal(x+j, y, bgr, bgg, bgb);
			}

			tmp <<= 1;
		}
		y++;
	}
}

void lcd_blit_char(uint8_t c, int x, int y, uint8_t r, uint8_t g, uint8_t b,
		uint8_t bgr, uint8_t bgg, uint8_t bgb)
{
	lcd_blit_char_internal(c, x, y, r, g, b, bgr, bgg, bgb);
}

int lcd_blit_string(char *str, int x, int y, uint8_t r, uint8_t g, uint8_t b,
		uint8_t bgr, uint8_t bgg, uint8_t bgb) {
	while ((*str) && (x <= 150)) {
		lcd_blit_char_internal(*str, x, y, r, g, b, bgr, bgg, bgb);
		str++; x+=9;
	}

	return x;
}

void lcd_refresh()
{
	lcd_send_data_bulk(lcd_fbuf, sizeof(lcd_fbuf));
}

void lcd_signalerror()
{
	lcd_send_data_singlecolor(15, 3, 3, sizeof(lcd_fbuf));

	delay_ms(45);

	lcd_refresh();
}

void lcd_test_pattern()
{
	/* red increasing by y, green increasing by x */
	for (int i = 0; i < 16; i++) {
		for (int j=0; j<160; j++) {
			lcd_blit_internal(j, i, i, j/10, 0x0);
		}
	}

	/* blue increasing by y, green increasing by x */
	for (int i = 16; i < 32; i++) {
		for (int j=0; j<160; j++) {
			lcd_blit_internal(j, i, 0, j/10, i-16);
		}
	}

	/* red increasing by y, blue increasing by x */
	for (int i = 32; i < 48; i++) {
		for (int j=0; j<160; j++) {
			lcd_blit_internal(j, i, i-32, 0, j/10);
		}
	}

	/* Gray increasing by x */
	for (int i = 48; i < 64; i++) {
		for (int j=0; j<160; j++) {
			lcd_blit_internal(j, i, j/10, j/10, j/10);
		}
	}

	/* Red increasing by x */
	for (int i = 64; i < 68; i++) {
		for (int j=0; j<160; j++) {
			lcd_blit_internal(j, i, j/10, 0, 0);
		}
	}

	/* Red increasing by x */
	for (int i = 68; i < 72; i++) {
		for (int j=0; j<160; j++) {
			lcd_blit_internal(j, i, 0, j/10, 0);
		}
	}

	/* Red increasing by x */
	for (int i = 72; i < 76; i++) {
		for (int j=0; j<160; j++) {
			lcd_blit_internal(j, i, 0, 0, j/10);
		}
	}

	lcd_refresh();
}

void lcd_init()
{
	// Initially, in reset.
	DIOSetOutput(lcd_rst, false, DIO_DRIVE_STRONG, false);
	DIOSetOutput(lcd_cs, false, DIO_DRIVE_STRONG, true); // XXX not selected, right?
	DIOSetOutput(lcd_a0, false, DIO_DRIVE_WEAK, true);
	DIOSetAltfuncOutput(lcd_mosi, 5, false, DIO_DRIVE_STRONG);
	DIOSetAltfuncOutput(lcd_sck, 5, false, DIO_DRIVE_STRONG);

	// Configure SPI

	lcd_spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_CPOL |
		(1 << 3 /*SPI_CR1_BR_Pos*/);
	lcd_spi->CR2 = 0;
	lcd_spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_CPOL |
		(1 << 3 /*SPI_CR1_BR_Pos*/) | SPI_CR1_SPE;

	// After 50ms, exit reset
	delay_ms(50);
	DIOHigh(lcd_rst);

	delay_ms(100);

	lcd_send_command(0x11);		// Sleep out command
	delay_ms(220);

	lcd_send_command(0x20);		// set noninverted

	lcd_send_command(0x36);		// set orientation / scan directions
	lcd_send_data(0x60);
	
	lcd_send_command(0x3a);
	lcd_send_data(0x03);		// selects reduced 4/4/4 data XXX

	lcd_send_command(0x26);
	lcd_send_data(0x04);		// Hopefully select a reasonable gamma

	lcd_send_command(0x29);

	lcd_send_command(0x2a);
	lcd_send_data(0);
	lcd_send_data(0);
	lcd_send_data(0);
	lcd_send_data(159);

	lcd_send_command(0x2b);
	lcd_send_data(0);
	lcd_send_data(0);
	lcd_send_data(0);
	lcd_send_data(127);

	lcd_send_command(0x2c);

	/* Draw black screen */
	lcd_refresh();
}
