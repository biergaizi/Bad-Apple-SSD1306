#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/timeb.h>
#include "bcm2835.h"

#define NBYTES ((128*64)/8)

#define A0 RPI_V2_GPIO_P1_18
#define RST RPI_V2_GPIO_P1_16

void lcm_init();
void lcm_clear();
void lcm_set_command();
void lcm_set_data();
void lcm_image(unsigned char *imgdata);

inline void lcm_set_command() {bcm2835_gpio_write(A0, LOW);}
inline void lcm_set_data() {bcm2835_gpio_write(A0, HIGH);}

void lcm_init()
{
    bcm2835_spi_begin();
    bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE3);
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_64);
    /* 64=250ns=4MHz | works up to 16 | unstable 8 | unusable 4 */
    bcm2835_spi_chipSelect(BCM2835_SPI_CS_NONE);

    bcm2835_gpio_fsel(A0, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(RST, BCM2835_GPIO_FSEL_OUTP);

    bcm2835_delayMicroseconds(1);

    // Reset
    bcm2835_gpio_write(RST, LOW);
    bcm2835_delayMicroseconds(10);
    bcm2835_gpio_write(RST, HIGH);

    lcm_set_command();
    bcm2835_spi_transfer(0xAE); // turn off the screen

    bcm2835_spi_transfer(0xD5); // set display clock divide ratio/oscillator frequency
    bcm2835_spi_transfer(0x80); // suggested ratio: 0x80

    bcm2835_spi_transfer(0xA8); // set multiplex
    bcm2835_spi_transfer(0x3F); // height: 64

    bcm2835_spi_transfer(0xD3); // set display offset
    bcm2835_spi_transfer(0x00); // offset: 0

    bcm2835_spi_transfer(0x40 | 0x0); // set start line

    bcm2835_spi_transfer(0x8D); // set charge pump
    bcm2835_spi_transfer(0x14); // charge pump: disable

    bcm2835_spi_transfer(0x20); // set memory mode
    bcm2835_spi_transfer(0x00); // mode: 0

    bcm2835_spi_transfer(0xA0 | 0x1); // set seg/column mapping

    bcm2835_spi_transfer(0xC8); // set com/row scan direction

    bcm2835_spi_transfer(0xDA); // set com pins hardware configuration
    bcm2835_spi_transfer(0x12); // compins: 0x12

    bcm2835_spi_transfer(0x81); // set contrast control register
    bcm2835_spi_transfer(0xCF); // brightness: 0xCF

    bcm2835_spi_transfer(0xD9); // set pre-charge period
    bcm2835_spi_transfer(0xF1); // pre-charge: 15 clocks / discharge: 1 clock

    bcm2835_spi_transfer(0XDB); // set vcom detect
    bcm2835_spi_transfer(0x40); // vcom deselect level: 0x40

    bcm2835_spi_transfer(0xA4); // disable entire display

    bcm2835_spi_transfer(0xA6); // disable inverse display (normal display)

    bcm2835_spi_transfer(0xAF); // turn on the screen

    lcm_clear();
}

void lcm_clear()
{
    int i;
    unsigned char page;
    for (page=0; page<8; page++)
    {
        lcm_set_command();
        bcm2835_spi_transfer(0xB0 + page);
        bcm2835_spi_transfer(0x01);
        bcm2835_spi_transfer(0x10);
        lcm_set_data();
        for (i=0; i<128; i++)
            bcm2835_spi_transfer(0);
    }
}

void lcm_image(unsigned char *imgdata)
{
    unsigned char *p;
    int i, j;
    p = imgdata;
    for (i=0; i<8; i++) {
        lcm_set_command();
        bcm2835_spi_transfer(0xB0 + i);
        bcm2835_spi_transfer(0x01);
        bcm2835_spi_transfer(0x10);
        lcm_set_data();
        for (j=0; j<128; j++) {
            bcm2835_spi_transfer(*p++);
        }
    }
}

unsigned char *imgbuffer;
int loadmovie(char *filename) {
    FILE *fp;
    int readbytes;
    int iFrame, nFrame;

    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Open file error\n");
        exit(2);
    }
    fseek(fp, 0L, SEEK_END);
    nFrame = ftell(fp) / NBYTES;
    fseek(fp, 0L, SEEK_SET);
    imgbuffer = malloc(sizeof(unsigned char) * nFrame * NBYTES);
    if (imgbuffer == NULL) {
        printf("Memory malloc error\n");
        exit(4);
    }

    for (iFrame=0; iFrame<nFrame; iFrame++)
    {
        readbytes = fread(imgbuffer+(iFrame*NBYTES), sizeof(unsigned char), NBYTES, fp);
        if (readbytes != NBYTES) {
            printf("Read error on: frame %d\n", iFrame);
            exit(3);
        }
    }
    fclose(fp);
    fp = NULL;

    return nFrame;
}

int main(int argc, char **argv)
{
    int i, n;
    int startsec;
    int millinext, millicurr;
    int sec;
    struct timeb tc;
    int framerate, frameinc, framediff;
    char framecorr[60];
    int fri, frc, frp;
    char audiocmd[255];

    if (argc <= 3) {
        printf("usage: 12864 [video_file] [audio_file] [framerate]\n");
        exit(1);
    }

    if (!bcm2835_init()) {printf("bcm2835 init error\n"); return 1;}
    lcm_init();

    n = loadmovie("raspi.bin");
    printf("Raspberry Pi & 12864 LCD Module Movie Player\n");
    lcm_image(imgbuffer);
    free(imgbuffer);
    bcm2835_delay(2000);

    sscanf(argv[3], "%d", &framerate);
    if (framerate > 60 || framerate <= 1) {
        printf("Framerate error\n");
        exit(5);
    }
    frameinc = 1000 / framerate;
    framediff = 1000 - frameinc*framerate;
    memset(framecorr, 0, 60);
    if (framediff != 0) {
        frc = 0; frp = framerate/framediff;
        for (fri=0; fri<framerate; fri+=frp) {
            framecorr[fri] = 1;
            frc++;
            if (frc >= framediff) break;
        }
    }

    strcpy(audiocmd, "mpg123 -q -b 16 \"");
    strcat(audiocmd, argv[2]);
    strcat(audiocmd, "\" >> /dev/null &");
    system(audiocmd);

    n = loadmovie(argv[1]);
    printf("Movie Start (Frames: %d)\n", n);

    //while (1) { /* add this for endless loop */
    sec = 0;
    ftime(&tc);
    startsec = tc.time;
    millinext = (int)(tc.millitm) + framerate;
    for (i=0; i<n; i++)
    {
        if (i % framerate == 0) {
            fflush(stdout);
            printf("\r%d'%2d\"  ", sec/60, sec%60);
            sec++;
        }
        lcm_image(imgbuffer+(i*NBYTES));
        while (1)
        {
            ftime(&tc);
            millicurr = (tc.time-startsec)*1000 + tc.millitm;
            if (millicurr >= millinext) {
                millinext += frameinc;
                if (framecorr[i%framerate]) millinext++;
                break;
            }
        }
    }
    //}

    free(imgbuffer);
    imgbuffer = NULL;
    printf("\nMovie End. Have a nice day! \n");

    return 0;
}

