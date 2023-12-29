#include <string.h>

#include <armdio.h>
#include <delayutil.h>
#include <font8x13.h>
#include <stm32f4xx.h>

/* For ST7735S 160x128 display */


static DIOTag_t lcd_rst = GPIOB_DIO(7);
static DIOTag_t lcd_cs = GPIOB_DIO(10);
static DIOTag_t lcd_a0 = GPIOB_DIO(6);
static DIOTag_t lcd_mosi = GPIOB_DIO(15); // AF05
static DIOTag_t lcd_sck = GPIOB_DIO(13);  // AF05
static SPI_TypeDef *lcd_spi = SPI2;

/* DMA1 Stream 4 Channel 0: SPI TX */
static DMA_Stream_TypeDef *lcd_dma_stream = DMA1_Stream4;
static const uint32_t lcd_dma_channel = DMA_Channel_0;
static const uint32_t lcd_dma_tcif = DMA_FLAG_TCIF4;
static const uint32_t lcd_dma_flags = DMA_FLAG_FEIF4 | DMA_FLAG_DMEIF4 |
		DMA_FLAG_TEIF4 | DMA_FLAG_HTIF4 | lcd_dma_tcif;

static uint32_t lcd_framecount;

static uint8_t lcd_fbuf[160*128*3/2];

static inline void lcd_spi_waitcompletion()
{
	while (!(lcd_spi->SR & SPI_SR_TXE));
	while ((lcd_spi->SR & SPI_SR_BSY));
}

static inline void lcd_send_command(uint8_t cmd)
{
	DIOHigh(lcd_cs);
	delay_loop(200);
	DIOLow(lcd_a0);	// Low for command
	delay_loop(100);
	DIOLow(lcd_cs);
	delay_loop(100);
	lcd_spi->DR = cmd; // send SPI

	lcd_spi_waitcompletion();

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

	lcd_spi_waitcompletion();
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

static inline void lcd_setup_dma(uint8_t *data, int len)
{
	uintptr_t raw_src = (uintptr_t) data;

	DMA_ClearFlag(lcd_dma_stream, lcd_dma_flags);
	DMA_DeInit(lcd_dma_stream);
	DMA_Cmd(lcd_dma_stream, DISABLE);

	DMA_InitTypeDef dma_init = {0};

	dma_init.DMA_Channel = lcd_dma_channel;
	dma_init.DMA_PeripheralBaseAddr = (uintptr_t) &lcd_spi->DR;
	dma_init.DMA_Memory0BaseAddr = raw_src;
	dma_init.DMA_DIR = DMA_DIR_MemoryToPeripheral;
	dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
	dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	dma_init.DMA_MemoryBurst = DMA_MemoryBurst_Single;
	dma_init.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
	dma_init.DMA_BufferSize = len;

	DMA_Init(lcd_dma_stream, &dma_init);
	DMA_Cmd(lcd_dma_stream, ENABLE);
}

static inline void lcd_send_data_bulk(uint8_t *data, int len)
{
	delay_loop(100);
	DIOHigh(lcd_a0);
	delay_loop(100);
	DIOLow(lcd_cs);
	delay_loop(100);

	lcd_setup_dma(data, len);
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

void lcd_getcolor(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b)
{
	int addr = y * 160 + x;
	addr *= 3;
	addr /= 2;

	if (x % 2) {
		*r = (lcd_fbuf[addr]) & 0x0f;
		*g = (lcd_fbuf[addr+1] >> 4) & 0x0f;
		*b = (lcd_fbuf[addr+1]) & 0x0f;
	} else {
		*r = (lcd_fbuf[addr] >> 4) & 0x0f;
		*g = (lcd_fbuf[addr]) & 0x0f;
		*b = (lcd_fbuf[addr+1] >> 4) & 0x0f;
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

int lcd_blit_string(const char *str, int x, int y, uint8_t r, uint8_t g, uint8_t b,
		uint8_t bgr, uint8_t bgg, uint8_t bgb) {
	while ((*str) && (x <= 150)) {
		lcd_blit_char_internal(*str, x, y, r, g, b, bgr, bgg, bgb);
		str++; x+=9;
	}

	return x;
}

static inline void lcd_dma_wait_finish()
{
	if (DMA_GetCmdStatus(lcd_dma_stream) == DISABLE) {
		return;
	}

	//while (DMA_GetFlagStatus(lcd_dma_stream, lcd_dma_tcif) == RESET);

	lcd_spi_waitcompletion();

	delay_loop(100);
	DIOHigh(lcd_cs);
}

bool lcd_is_ready()
{
	if (DMA_GetCmdStatus(lcd_dma_stream) == DISABLE) {
		return true;
	}

	return false;
}

void lcd_refresh()
{
	lcd_dma_wait_finish();

	lcd_framecount++;

	if (!(lcd_framecount & 15)) {
		lcd_send_command(0x2c);		// Ram WRITE command
	}

	lcd_send_data_bulk(lcd_fbuf, sizeof(lcd_fbuf));
}

void lcd_signalerror()
{
	lcd_dma_wait_finish();

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
	DIOSetOutput(lcd_cs, false, DIO_DRIVE_STRONG, true); // (not selected)
	DIOSetOutput(lcd_a0, false, DIO_DRIVE_WEAK, true);
	DIOSetAltfuncOutput(lcd_mosi, 5, false, DIO_DRIVE_STRONG);
	DIOSetAltfuncOutput(lcd_sck, 5, false, DIO_DRIVE_STRONG);

	// Configure SPI

	lcd_spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_CPOL | SPI_CR1_CPHA |
		(1 << 3 /*SPI_CR1_BR_Pos*/);
	lcd_spi->CR2 = 0;
	lcd_spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_CPOL | SPI_CR1_CPHA |
		(1 << 3 /*SPI_CR1_BR_Pos*/) | SPI_CR1_SPE;
	lcd_spi->CR2 = SPI_CR2_TXDMAEN;

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
	lcd_send_data(0x03);		// selects reduced 4/4/4 data

	lcd_send_command(0x26);
	lcd_send_data(0x04);		// Hopefully select a reasonable gamma

	lcd_send_command(0x29);		// Display on

	lcd_send_command(0x2a);		// Column addresses -- 0 .. 159
	lcd_send_data(0);
	lcd_send_data(0);
	lcd_send_data(0);
	lcd_send_data(159);

	lcd_send_command(0x2b);		// Row addresses -- 0 .. 127
	lcd_send_data(0);
	lcd_send_data(0);
	lcd_send_data(0);
	lcd_send_data(127);

	lcd_send_command(0x2c);		// Ram WRITE command

	/* Draw black screen */
	lcd_refresh();
}
