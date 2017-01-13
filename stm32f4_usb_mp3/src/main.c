#include "main.h"
#include "core_cm4.h"
#include "stm32f4xx_conf.h"
#include "mp3dec.h"
#include "Audio.h"
#include <string.h>

// Macros
#define f_tell(fp)		((fp)->fptr)
#define BUTTON			(GPIOA->IDR & GPIO_Pin_0)

// Variables
volatile uint32_t time_var1, time_var2;
USB_OTG_CORE_HANDLE USB_OTG_Core;
USBH_HOST USB_Host;
RCC_ClocksTypeDef RCC_Clocks;
volatile int enum_done = 0;
volatile uint8_t intr_change_song = 0; // indicates if a request to change song has arrived via interrupt
volatile uint8_t is_playing = 1; // indicates if music is currently being played
volatile int volume = 180;
volatile int8_t is_mute = 0;

// MP3 Variables
#define FILE_READ_BUFFER_SIZE 8192
MP3FrameInfo mp3FrameInfo;
HMP3Decoder hMP3Decoder;
FIL file;
char file_read_buffer[FILE_READ_BUFFER_SIZE];
volatile int bytes_left;
char *read_ptr;
char title_f[125];

// Directory Variables
volatile uint32_t seek_to_resume = 0;

// Private function prototypes
static void AudioCallback(void *context, int buffer);
static uint32_t Mp3ReadId3V2Tag(FIL* pInFile, char* pszArtist,
		uint32_t unArtistSize, char* pszTitle, uint32_t unTitleSize);
static void play_mp3(char* filename);
static FRESULT play_directory(const char* path, unsigned char seek);

GPIO_InitTypeDef GPIO_InitStructure;
USART_InitTypeDef USART_InitStructure;
NVIC_InitTypeDef NVIC_InitStructure; // this is used to configure the NVIC (nested vector interrupt controller)

#define MAX_STRLEN 12 // this is the maximum string length of our string in characters
volatile char received_string[MAX_STRLEN + 1]; // this will hold the recieved string

void USART_puts(USART_TypeDef* USARTx, volatile char *s) {

	while (*s) {
		// wait until data register is empty
		while (!(USARTx->SR & 0x00000040))
			;
		while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
			;
		USART_SendData(USARTx, *s);
		*s++;
	}
}

void USART_Configuration(void) {
	// sort out clocks
	RCC_AHB1PeriphClockCmd( RCC_AHB1Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
	/* Configure USART2 Tx (PA.02) as alternate function push-pull */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	// Map USART2 to A.02
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);
	// Initialize USART
	USART_InitStructure.USART_BaudRate = 28800;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl =
			USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
	/* Configure USART */
	USART_Init(USART2, &USART_InitStructure);

	//USART2_IRQHandler() interrupt and configure
	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	/* Enable the USART */
	USART_Cmd(USART2, ENABLE);
}

volatile uint32_t msTicks; // Counts 1ms timeTicks

void SysTick_Handler(void) {
	msTicks++;                                    // increment Delay()-counter
}

/*
 * Main function. Called when startup code is done with
 * copying memory and setting up clocks.
 */
int main(void) {
	// GPIOD Peripheral clock enable
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

	// Configure PD12, PD13, PD14 and PD15 in output pushpull mode
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14
			| GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

//	SystemInit();

	// Initialize USB Host Library
	USBH_Init(&USB_OTG_Core, USB_OTG_FS_CORE_ID, &USB_Host, &USBH_MSC_cb,
			&USR_Callbacks);

	/* USART Configuration */
	USART_Configuration();

	/*
	 while (1)
	 {
	 ////////////////////////////////////////////////////////////////////
	 //		//read data from terminal
	 while(USART_GetFlagStatus(USART2, USART_FLAG_RXNE)==RESET); //Receive data register not empty flag
	 USART_SendData(USART2, USART_ReceiveData(USART2));

	 /////////////////////////////////////////////////////////////////////

	 //USART_puts(USART2, "\rGot it.\n\r");
	 //putcharx(65);
	 //Delay1(1000);
	 //Delay(10000);
	 }
	 */

	for (;;) {
		USBH_Process(&USB_OTG_Core, &USB_Host);

		if (enum_done >= 2) {
			enum_done = 0;
			play_directory("", 0);
		}
	}
}

void USART2_IRQHandler(void) {

	// check if the USART1 receive interrupt flag was set
	if (USART_GetITStatus(USART2, USART_IT_RXNE)) {

//		static uint8_t cnt = 0; // this counter is used to determine the string length
		char t = USART2->DR; // the character from the USART2 data register is saved in t
		if (t == '1') { // previous song
			if (seek_to_resume > 0)
				seek_to_resume--;
			intr_change_song = 1;
		} else if (t == '2') { // play/pause
			if (is_playing) {
				AudioOff();
				is_playing = 0;
			} else {
				AudioOn();
				is_playing = 1;
			}
		} else if (t == '3') { // next song
			seek_to_resume++;
			intr_change_song = 1;
		} else if (t == '4') { // toggle mute
			if (is_mute) {
				SetAudioVolume(volume);
				is_mute = 0;
			} else {
				SetAudioVolume(0);
				is_mute = 1;
			}
		} else if (t == '5') { // volume down
			volume -= 15;
			is_mute = 0;
			if (volume < 0)
				volume = 0;
			SetAudioVolume(volume);
		} else if (t == '6') { // volume up
			volume += 15;
			is_mute = 0;
			if (volume > 255)
				volume = 255;
			SetAudioVolume(volume);
		} else if (t == '0') { // root, send title info
			USART_puts(USART2, title_f);
		}

		/* check if the received character is not the LF character (used to determine end of string)
		 * or the if the maximum string length has been been reached
		 */

//		if( (t != '\r') && (cnt < MAX_STRLEN) ){
//			received_string[cnt] = t;
//			cnt++;
//		}
//		else{ // otherwise reset the character counter and print the received string
//			received_string[cnt] = '\0';
//			cnt = 0;
//			USART_puts(USART2, received_string);
//		}
	}
}

const char *get_filename_ext(const char *filename) {
	const char *dot = strrchr(filename, '.');
	if (!dot || dot == filename)
		return "";
	return dot + 1;
}

static FRESULT play_directory(const char* path, unsigned char seek) {
	FRESULT res;
	FILINFO fno;
	DIR dir;
	char *fn; /* This function is assuming non-Unicode cfg. */
	char buffer[200];
#if _USE_LFN
	static char lfn[_MAX_LFN + 1];
	fno.lfname = lfn;
	fno.lfsize = sizeof(lfn);
#endif
	uint32_t seek_counter = 0;

	res = f_opendir(&dir, path); /* Open the directory */
	if (res == FR_OK) {
		for (;;) {
			res = f_readdir(&dir, &fno); /* Read a directory item */
			if (res != FR_OK || fno.fname[0] == 0)
				break; /* Break on error or end of dir */
			if (fno.fname[0] == '.')
				continue; /* Ignore dot entry */
#if _USE_LFN
			fn = *fno.lfname ? fno.lfname : fno.fname;
#else
			fn = fno.fname;
#endif
			if (fno.fattrib & AM_DIR) { /* It is a directory */

			} else { /* It is a file. */
				sprintf(buffer, "%s/%s", path, fn);

				// Check if it is an mp3 file
				if (strcmp("mp3", get_filename_ext(buffer)) == 0) {

					// Skip "seek" number of mp3 files...
					if (seek) {
						seek--;
						continue;
					}

					// seek_to_resume skips the given number of songs. it makes the "previous song" functionality possible
					if (seek_to_resume - seek_counter) {
						seek_counter++;
						continue;
					}

					play_mp3(buffer);
					f_opendir(&dir, path); /* reset the directory so that seek_to_resume functions properly */
					seek_counter = 0;
				}
			}
		}
	}

	return res;
}

static void play_mp3(char* filename) {
	unsigned int br, btr;
	FRESULT res;

	bytes_left = FILE_READ_BUFFER_SIZE;
	read_ptr = file_read_buffer;

	if (FR_OK == f_open(&file, filename, FA_OPEN_EXISTING | FA_READ)) {

		// Read ID3v2 Tag
		char szArtist[120];
		char szTitle[120];
		Mp3ReadId3V2Tag(&file, szArtist, sizeof(szArtist), szTitle,
				sizeof(szTitle));

		// send song title via USART
//		char title_f[strlen(szArtist)+ strlen(szTitle)+5]; // +5: 3 for " - ", 1 for "|", 1 extra
		strcpy(title_f, szArtist);
		strcat(title_f, " - ");
		strcat(title_f, szTitle);
		strcat(title_f, "|");

		// Fill buffer
		f_read(&file, file_read_buffer, FILE_READ_BUFFER_SIZE, &br);

		// Play mp3
		hMP3Decoder = MP3InitDecoder();
		InitializeAudio(Audio44100HzSettings);
		SetAudioVolume(volume);
		PlayAudioWithCallback(AudioCallback, 0);

		USART_puts(USART2, title_f);

		for (;;) {
			/*
			 * If past half of buffer, refill...
			 *
			 * When bytes_left changes, the audio callback has just been executed. This
			 * means that there should be enough time to copy the end of the buffer
			 * to the beginning and update the pointer before the next audio callback.
			 * Getting audio callbacks while the next part of the file is read from the
			 * file system should not cause problems.
			 */
			if (bytes_left < (FILE_READ_BUFFER_SIZE / 2)) {
				// Copy rest of data to beginning of read buffer
				memcpy(file_read_buffer, read_ptr, bytes_left);

				// Update read pointer for audio sampling
				read_ptr = file_read_buffer;

				// Read next part of file
				btr = FILE_READ_BUFFER_SIZE - bytes_left;
				res = f_read(&file, file_read_buffer + bytes_left, btr, &br);

				// Update the bytes left variable
				bytes_left = FILE_READ_BUFFER_SIZE;

				// Out of data or error or user button... Stop playback!
				if (br < btr || res != FR_OK || BUTTON || intr_change_song) {
					StopAudio();

					// Re-initialize and set volume to avoid noise
					InitializeAudio(Audio44100HzSettings);
					SetAudioVolume(0);

					// Close currently open file
					f_close(&file);

					// Wait for user button release
					while (BUTTON) {
					};

					// the next song will be played
					if (!intr_change_song) {
						seek_to_resume++;
					}

					// handle request
					intr_change_song = 0;

					// Return to previous function
					return;
				}
			}
		}
	}
}

/*
 * Called by the audio driver when it is time to provide data to
 * one of the audio buffers (while the other buffer is sent to the
 * CODEC using DMA). One mp3 frame is decoded at a time and
 * provided to the audio driver.
 */
static void AudioCallback(void *context, int buffer) {
	static int16_t audio_buffer0[4096];
	static int16_t audio_buffer1[4096];

	int offset, err;
	int outOfData = 0;

	int16_t *samples;
	if (buffer) {
		samples = audio_buffer0;
		GPIO_SetBits(GPIOD, GPIO_Pin_13);
		GPIO_ResetBits(GPIOD, GPIO_Pin_14);
	} else {
		samples = audio_buffer1;
		GPIO_SetBits(GPIOD, GPIO_Pin_14);
		GPIO_ResetBits(GPIOD, GPIO_Pin_13);
	}

	offset = MP3FindSyncWord((unsigned char*) read_ptr, bytes_left);
	bytes_left -= offset;
	read_ptr += offset;

	err = MP3Decode(hMP3Decoder, (unsigned char**) &read_ptr,
			(int*) &bytes_left, samples, 0);

	if (err) {
		/* error occurred */
		switch (err) {
		case ERR_MP3_INDATA_UNDERFLOW:
			outOfData = 1;
			break;
		case ERR_MP3_MAINDATA_UNDERFLOW:
			/* do nothing - next call to decode will provide more mainData */
			break;
		case ERR_MP3_FREE_BITRATE_SYNC:
		default:
			outOfData = 1;
			break;
		}
	} else {
		// no error
		MP3GetLastFrameInfo(hMP3Decoder, &mp3FrameInfo);

		// Duplicate data in case of mono to maintain playback speed
		if (mp3FrameInfo.nChans == 1) {
			for (int i = mp3FrameInfo.outputSamps; i >= 0; i--) {
				samples[2 * i] = samples[i];
				samples[2 * i + 1] = samples[i];
			}
			mp3FrameInfo.outputSamps *= 2;
		}
	}

	if (!outOfData) {
		ProvideAudioBuffer(samples, mp3FrameInfo.outputSamps);
	}
}

/*
 * Called by the SysTick interrupt
 */
void TimingDelay_Decrement(void) {
	if (time_var1) {
		time_var1--;
	}
	time_var2++;
}

/*
 * Delay a number of systick cycles (1ms)
 */

void Delay(uint32_t dlyTicks) {
	uint32_t curTicks;

	SysTick_Config(8000000 / 1000); // we're operating at 48 MHz
	curTicks = msTicks;
	while ((msTicks - curTicks) < dlyTicks)
		; // wait here until our time has come...
}

/*
 * Dummy function to avoid compiler error
 */
void _init() {

}

/*
 * Taken from
 * http://www.mikrocontroller.net/topic/252319
 */
static uint32_t Mp3ReadId3V2Text(FIL* pInFile, uint32_t unDataLen,
		char* pszBuffer, uint32_t unBufferSize) {
	UINT unRead = 0;
	BYTE byEncoding = 0;
	if ((f_read(pInFile, &byEncoding, 1, &unRead) == FR_OK) && (unRead == 1)) {
		unDataLen--;
		if (unDataLen <= (unBufferSize - 1)) {
			if ((f_read(pInFile, pszBuffer, unDataLen, &unRead) == FR_OK)
					|| (unRead == unDataLen)) {
				if (byEncoding == 0) {
					// ISO-8859-1 multibyte
					// just add a terminating zero
					pszBuffer[unDataLen] = 0;
				} else if (byEncoding == 1) {
					// UTF16LE unicode
					uint32_t r = 0;
					uint32_t w = 0;
					if ((unDataLen > 2) && (pszBuffer[0] == 0xFF)
							&& (pszBuffer[1] == 0xFE)) {
						// ignore BOM, assume LE
						r = 2;
					}
					for (; r < unDataLen; r += 2, w += 1) {
						// should be acceptable for 7 bit ascii
						pszBuffer[w] = pszBuffer[r];
					}
					pszBuffer[w] = 0;
				}
			} else {
				return 1;
			}
		} else {
			// we won't read a partial text
			if (f_lseek(pInFile, f_tell(pInFile) + unDataLen) != FR_OK) {
				return 1;
			}
		}
	} else {
		return 1;
	}
	return 0;
}

/*
 * Taken from
 * http://www.mikrocontroller.net/topic/252319
 */
static uint32_t Mp3ReadId3V2Tag(FIL* pInFile, char* pszArtist,
		uint32_t unArtistSize, char* pszTitle, uint32_t unTitleSize) {
	pszArtist[0] = 0;
	pszTitle[0] = 0;

	BYTE id3hd[10];
	UINT unRead = 0;
	if ((f_read(pInFile, id3hd, 10, &unRead) != FR_OK) || (unRead != 10)) {
		return 1;
	} else {
		uint32_t unSkip = 0;
		if ((unRead == 10) && (id3hd[0] == 'I') && (id3hd[1] == 'D')
				&& (id3hd[2] == '3')) {
			unSkip += 10;
			unSkip = ((id3hd[6] & 0x7f) << 21) | ((id3hd[7] & 0x7f) << 14)
					| ((id3hd[8] & 0x7f) << 7) | (id3hd[9] & 0x7f);

			// try to get some information from the tag
			// skip the extended header, if present
			uint8_t unVersion = id3hd[3];
			if (id3hd[5] & 0x40) {
				BYTE exhd[4];
				f_read(pInFile, exhd, 4, &unRead);
				size_t unExHdrSkip = ((exhd[0] & 0x7f) << 21)
						| ((exhd[1] & 0x7f) << 14) | ((exhd[2] & 0x7f) << 7)
						| (exhd[3] & 0x7f);
				unExHdrSkip -= 4;
				if (f_lseek(pInFile, f_tell(pInFile) + unExHdrSkip) != FR_OK) {
					return 1;
				}
			}
			uint32_t nFramesToRead = 2;
			while (nFramesToRead > 0) {
				char frhd[10];
				if ((f_read(pInFile, frhd, 10, &unRead) != FR_OK)
						|| (unRead != 10)) {
					return 1;
				}
				if ((frhd[0] == 0) || (strncmp(frhd, "3DI", 3) == 0)) {
					break;
				}
				char szFrameId[5] = { 0, 0, 0, 0, 0 };
				memcpy(szFrameId, frhd, 4);
				uint32_t unFrameSize = 0;
				uint32_t i = 0;
				for (; i < 4; i++) {
					if (unVersion == 3) {
						// ID3v2.3
						unFrameSize <<= 8;
						unFrameSize += frhd[i + 4];
					}
					if (unVersion == 4) {
						// ID3v2.4
						unFrameSize <<= 7;
						unFrameSize += frhd[i + 4] & 0x7F;
					}
				}

				if (strcmp(szFrameId, "TPE1") == 0) {
					// artist
					if (Mp3ReadId3V2Text(pInFile, unFrameSize, pszArtist,
							unArtistSize) != 0) {
						break;
					}
					nFramesToRead--;
				} else if (strcmp(szFrameId, "TIT2") == 0) {
					// title
					if (Mp3ReadId3V2Text(pInFile, unFrameSize, pszTitle,
							unTitleSize) != 0) {
						break;
					}
					nFramesToRead--;
				} else {
					if (f_lseek(pInFile, f_tell(pInFile) + unFrameSize)
							!= FR_OK) {
						return 1;
					}
				}
			}
		}
		if (f_lseek(pInFile, unSkip) != FR_OK) {
			return 1;
		}
	}

	return 0;
}

