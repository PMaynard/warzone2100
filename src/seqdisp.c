/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2009  Warzone Resurrection Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/**
 * @file seqdisp.c
 *
 * Functions for the display of the Escape Sequences (FMV).
 *
 */

#include "lib/framework/frame.h"
#include "lib/framework/frameint.h"

#include <string.h>
#include <SDL_timer.h>
#include <physfs.h>

#include "lib/framework/file.h"
#include "lib/framework/stdio_ext.h"
#include "lib/ivis_common/rendmode.h"
#include "lib/ivis_common/piemode.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/sequence/sequence.h"
#include "lib/sound/audio.h"
#include "lib/sound/cdaudio.h"
#include "lib/script/script.h"

#include "seqdisp.h"

#include "warzoneconfig.h"
#include "hci.h"//for font
#include "loop.h"
#include "scripttabs.h"
#include "design.h"
#include "wrappers.h"
#include "init.h" // For fileLoadBuffer
#include "drive.h"

/***************************************************************************/
/*
 *	local Definitions
 */
/***************************************************************************/
#define MAX_TEXT_OVERLAYS 32
#define MAX_SEQ_LIST	  6
#define SUBTITLE_BOX_MIN 430
#define SUBTITLE_BOX_MAX 480

// NOTE: The original game never had a true fullscreen mode for FMVs on >640x480 screens.
// They would just use double sized videos, and move the text to that area.
// Since we *do* offer fullscreen FMVs, this isn't really needed anymore, depending
// on how we want to handle the text.
static int D_W2 = 0;	// Text width offset
static int D_H2 = 0;	// Text height offset

typedef struct
{
	char pText[MAX_STR_LENGTH];
	UDWORD x;
	UDWORD y;
	UDWORD startFrame;
	UDWORD endFrame;
	BOOL	bSubtitle;
} SEQTEXT;

typedef struct
{
	const char	*pSeq;						//name of the sequence to play
	const char	*pAudio;					//name of the wav to play
	BOOL		bSeqLoop;					//loop this sequence
	SDWORD		currentText;				//cuurent number of text messages for this seq
	SEQTEXT		aText[MAX_TEXT_OVERLAYS];	//text data to display for this sequence
} SEQLIST;
/***************************************************************************/
/*
 *	local Variables
 */
/***************************************************************************/

static BOOL bBackDropWasAlreadyUp;
static BOOL bAudioPlaying = false;
static BOOL bHoldSeqForAudio = false;
static BOOL bSeqSubtitles = true;
static BOOL bSeqPlaying = false;
static const char aHardPath[] = "sequences/";
static char aVideoName[MAX_STR_LENGTH];
static char* pVideoBuffer = NULL;
static char* pVideoPalette = NULL;
static SDWORD frameSkip = 1;
static SEQLIST aSeqList[MAX_SEQ_LIST];
static SDWORD currentSeq = -1;
static SDWORD currentPlaySeq = -1;
static BOOL g_bResumeInGame = false;

/***************************************************************************/
/*
 *	local ProtoTypes
 */
/***************************************************************************/

typedef enum
{
	VIDEO_PRESELECTED_RESOLUTION,
	VIDEO_USER_CHOSEN_RESOLUTION,
} VIDEO_RESOLUTION;

static bool seq_StartFullScreenVideo(const char* videoName, const char* audioName, VIDEO_RESOLUTION resolution);
BOOL seq_SetupVideoBuffers(void);
BOOL seq_ReleaseVideoBuffers(void);

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/

 /* Renders a video sequence specified by filename to a buffer*/
bool seq_RenderVideoToBuffer(const char* sequenceName, int seqCommand)
{
	static enum
	{
		VIDEO_NOT_PLAYING,
		VIDEO_PLAYING,
		VIDEO_FINISHED,
	} videoPlaying = VIDEO_NOT_PLAYING;
	static enum
	{
		VIDEO_LOOP,
		VIDEO_HOLD_LAST_FRAME,
	} frameHold = VIDEO_LOOP;

	if (seqCommand == SEQUENCE_KILL)
	{
		//stop the movie
		seq_Shutdown();
		bSeqPlaying = false;
		frameHold = VIDEO_LOOP;
		videoPlaying = VIDEO_NOT_PLAYING;
		return true;
	}

	if (!bSeqPlaying
	 && frameHold == VIDEO_LOOP)
	{
		//start the ball rolling

		iV_SetFont(font_regular);
		iV_SetTextColour(WZCOL_TEXT_BRIGHT);

		/* We do *NOT* want to use the user-choosen resolution when we
		 * are doing intelligence videos.
		 */
		videoPlaying = seq_StartFullScreenVideo(sequenceName, NULL, VIDEO_PRESELECTED_RESOLUTION) ? VIDEO_PLAYING : VIDEO_FINISHED;
		bSeqPlaying = true;
	}

	if (videoPlaying != VIDEO_FINISHED)
	{
		videoPlaying = seq_Update() ? VIDEO_PLAYING : VIDEO_FINISHED;
	}

	if (videoPlaying == VIDEO_FINISHED)
	{
		seq_Shutdown();
		bSeqPlaying = false;
		frameHold = VIDEO_HOLD_LAST_FRAME;
		videoPlaying = VIDEO_NOT_PLAYING;
		return false;
	}

	return true;
}

BOOL seq_ReleaseVideoBuffers(void)
{
	free(pVideoBuffer);
	free(pVideoPalette);
	pVideoBuffer = NULL;
	pVideoPalette = NULL;
	return true;
}

BOOL seq_SetupVideoBuffers(void)
{
	return true;
}

static void seq_SetUserResolution(void)
{
	switch (war_GetFMVmode())
	{
		case FMV_1X:
		{
			// Native (1x)
			const int x = (screenWidth - 320) / 2;
			const int y = (screenHeight - 240) / 2;
			seq_SetDisplaySize(320, 240, x, y);
			break;
		}
		case FMV_2X:
		{
			// Double (2x)
			const int x = (screenWidth - 640) / 2;
			const int y = (screenHeight - 480) / 2;
			seq_SetDisplaySize(640, 480, x, y);
			break;
		}
		case FMV_FULLSCREEN:
			seq_SetDisplaySize(screenWidth, screenHeight, 0, 0);
			break;

		default:
			ASSERT(!"invalid FMV mode", "Invalid FMV mode: %u", (unsigned int)war_GetFMVmode());
			break;
	}
}

//full screenvideo functions
static bool seq_StartFullScreenVideo(const char* videoName, const char* audioName, VIDEO_RESOLUTION resolution)
{
	const char* aAudioName;
	int chars_printed;

	bHoldSeqForAudio = false;

	chars_printed = ssprintf(aVideoName, "%s%s", aHardPath, videoName);
	ASSERT(chars_printed < sizeof(aVideoName), "sequence path + name greater than max string");

	//set audio path
	if (audioName != NULL)
	{
		sasprintf((char**)&aAudioName, "sequenceaudio/%s", audioName);
	}

	cdAudio_Pause();
	iV_SetFont(font_regular);
	iV_SetTextColour(WZCOL_TEXT_BRIGHT);

	/* We do not want to enter loop_SetVideoPlaybackMode() when we are
	 * doing intelligence videos.
	 */
	if (resolution == VIDEO_USER_CHOSEN_RESOLUTION)
	{
		//start video mode
		if (loop_GetVideoMode() == 0)
		{
			// check to see if we need to pause, and set font each time
			cdAudio_Pause();
			loop_SetVideoPlaybackMode();
			iV_SetFont(font_regular);
			iV_SetTextColour(WZCOL_TEXT_BRIGHT);
		}

		// set the dimensions to show full screen or native or ...
		seq_SetUserResolution();
	}

	if (!seq_Play(aVideoName))
	{
		seq_Shutdown();
		return false;
	}

	if (audioName == NULL)
	{
		bAudioPlaying = false;
	}
	else
	{
		// NOT controlled by sliders for now?
		static const float maxVolume = 1.f;

		bAudioPlaying = audio_PlayStream(aAudioName, maxVolume, NULL, NULL) ? true : false;
		ASSERT(bAudioPlaying == true, "unable to initialise sound %s", aAudioName);
	}

	return true;
}

BOOL seq_UpdateFullScreenVideo(int *pbClear)
{
	int i;
	BOOL bMoreThanOneSequenceLine = false;
	bool stillPlaying;

	unsigned int subMin = SUBTITLE_BOX_MAX + D_H2;
	unsigned int subMax = SUBTITLE_BOX_MIN + D_H2;

	//get any text lines over bottom of the video
	unsigned int realFrame = seq_GetFrameNumber();
	for(i=0;i<MAX_TEXT_OVERLAYS;i++)
	{
		if (aSeqList[currentPlaySeq].aText[i].pText[0] != '\0')
		{
			if (aSeqList[currentPlaySeq].aText[i].bSubtitle)
			{
				if ((realFrame >= aSeqList[currentPlaySeq].aText[i].startFrame) && (realFrame <= aSeqList[currentPlaySeq].aText[i].endFrame))
				{
					if (subMin > aSeqList[currentPlaySeq].aText[i].y)
					{
						if (aSeqList[currentPlaySeq].aText[i].y > SUBTITLE_BOX_MIN)
						{
							subMin = aSeqList[currentPlaySeq].aText[i].y;
						}
					}
					if (subMax < aSeqList[currentPlaySeq].aText[i].y)
					{
						subMax = aSeqList[currentPlaySeq].aText[i].y;
					}
				}
				else if (aSeqList[currentPlaySeq].bSeqLoop)//if its a looped video always draw the text
				{
					if (subMin >= aSeqList[currentPlaySeq].aText[i].y)
					{
						if (aSeqList[currentPlaySeq].aText[i].y > SUBTITLE_BOX_MIN)
						{
							subMin = aSeqList[currentPlaySeq].aText[i].y;
						}
					}
					if (subMax < aSeqList[currentPlaySeq].aText[i].y)
					{
						subMax = aSeqList[currentPlaySeq].aText[i].y;
					}
				}
			}

			if (realFrame >= aSeqList[currentPlaySeq].aText[i].endFrame
			 && realFrame < aSeqList[currentPlaySeq].aText[i].endFrame + frameSkip)
			{
				if (pbClear != NULL)
				{
					*pbClear = CLEAR_BLACK;
				}
			}
		}
	}

	subMin -= D_H2;//adjust video window here because text is already ofset for big screens
	subMax -= D_H2;

	if (subMin < SUBTITLE_BOX_MIN)
	{
		subMin = SUBTITLE_BOX_MIN;
	}
	if (subMax > SUBTITLE_BOX_MAX)
	{
		subMax = SUBTITLE_BOX_MAX;
	}

	if (subMax > subMin)
	{
		bMoreThanOneSequenceLine = true;
	}

	//call sequence player to download last frame
	stillPlaying = seq_Update();
	//print any text over the video
	realFrame = seq_GetFrameNumber();//textFrame + 1;

	for(i=0;i<MAX_TEXT_OVERLAYS;i++)
	{
		if (aSeqList[currentPlaySeq].aText[i].pText[0] != '\0')
		{
			if ((realFrame >= aSeqList[currentPlaySeq].aText[i].startFrame) && (realFrame <= aSeqList[currentPlaySeq].aText[i].endFrame))
			{
				if (bMoreThanOneSequenceLine)
				{
					aSeqList[currentPlaySeq].aText[i].x = 20 + D_W2;
				}
				iV_DrawText(&(aSeqList[currentPlaySeq].aText[i].pText[0]),
						aSeqList[currentPlaySeq].aText[i].x, aSeqList[currentPlaySeq].aText[i].y);
			}
			else if (aSeqList[currentPlaySeq].bSeqLoop)//if its a looped video always draw the text
			{
				if (bMoreThanOneSequenceLine)
				{
					aSeqList[currentPlaySeq].aText[i].x = 20 + D_W2;
				}
				iV_DrawText(&(aSeqList[currentPlaySeq].aText[i].pText[0]),
						aSeqList[currentPlaySeq].aText[i].x, aSeqList[currentPlaySeq].aText[i].y);
			}

		}
	}
	if (!stillPlaying
	 || bHoldSeqForAudio)
	{
		if (bAudioPlaying)
		{
			if (aSeqList[currentPlaySeq].bSeqLoop)
			{
				seq_Shutdown();

				if (!seq_Play(aVideoName))
				{
					bHoldSeqForAudio = true;
				}
			}
			else
			{
				bHoldSeqForAudio = true;
			}
			return true;//should hold the video
		}
		else
		{
			return false;//should terminate the video
		}
	}

	return true;
}

BOOL seq_StopFullScreenVideo(void)
{
	StopDriverMode();
	if (!seq_AnySeqLeft())
	{
		loop_ClearVideoPlaybackMode();
	}

	seq_Shutdown();

	if (!seq_AnySeqLeft())
	{
		if ( g_bResumeInGame == true )
		{
			resetDesignPauseState();
			intAddReticule();
			g_bResumeInGame = false;
		}
	}

	return true;
}

// add a string at x,y or add string below last line if x and y are 0
BOOL seq_AddTextForVideo(const char* pText, SDWORD xOffset, SDWORD yOffset, SDWORD startFrame, SDWORD endFrame, SEQ_TEXT_POSITIONING textJustification)
{
	SDWORD sourceLength, currentLength;
	char* currentText;
	static SDWORD lastX;
	const unsigned int BUFFER_WIDTH = pie_GetVideoBufferWidth();

	iV_SetFont(font_regular);

	ASSERT(aSeqList[currentSeq].currentText < MAX_TEXT_OVERLAYS, "too many text lines");

	sourceLength = strlen(pText);
	currentLength = sourceLength;
	currentText = &(aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].pText[0]);

	//if the string is bigger than the buffer get the last end of the last fullword in the buffer
	if (currentLength >= MAX_STR_LENGTH)
	{
		currentLength = MAX_STR_LENGTH - 1;
		//get end of the last word
		while((pText[currentLength] != ' ') && (currentLength > 0))
		{
			currentLength--;
		}
		currentLength--;
	}

	memcpy(currentText,pText,currentLength);
	currentText[currentLength] = 0;//terminate the string what ever

	//check the string is shortenough to print
	//if not take a word of the end and try again
	while (iV_GetTextWidth(currentText) > BUFFER_WIDTH)
	{
		currentLength--;
		while((pText[currentLength] != ' ') && (currentLength > 0))
		{
			currentLength--;
		}
		currentText[currentLength] = 0;//terminate the string what ever
	}
	currentText[currentLength] = 0;//terminate the string what ever

	//check if x and y are 0 and put text on next line
	if (((xOffset == 0) && (yOffset == 0)) && (currentLength > 0))
	{
		aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].x = lastX;
		//	aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText-1].x;
		aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].y = aSeqList[currentSeq].
			aText[aSeqList[currentSeq].currentText-1].y + iV_GetTextLineSize();
	}
	else
	{
		aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].x = xOffset + D_W2;
		aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].y = yOffset + D_H2;
	}
	lastX = aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].x;

	if (textJustification
	 && currentLength == sourceLength)
	{
		static const int MIN_JUSTIFICATION = 40;
		static const int FOLLOW_ON_JUSTIFICATION = 160;
		//justify this text
		const int justification = BUFFER_WIDTH - iV_GetTextWidth(currentText);

		if (textJustification == SEQ_TEXT_JUSTIFY
		 && justification > MIN_JUSTIFICATION)
		{
			aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].x += (justification / 2);
		}
		else if (textJustification == SEQ_TEXT_FOLLOW_ON
		      && justification > FOLLOW_ON_JUSTIFICATION)
		{

		}
	}

	//set start and finish times for the objects
	aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].startFrame = startFrame;
	aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].endFrame = endFrame;
	aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].bSubtitle = textJustification;

	aSeqList[currentSeq].currentText++;
	if (aSeqList[currentSeq].currentText >= MAX_TEXT_OVERLAYS)
	{
		aSeqList[currentSeq].currentText = 0;
	}

	//check text is okay on the screen
	if (currentLength < sourceLength)
	{
		//RECURSE x= 0 y = 0 for nextLine
		if (textJustification == SEQ_TEXT_JUSTIFY)
		{
			textJustification = SEQ_TEXT_POSITION;
		}
		seq_AddTextForVideo(&pText[currentLength + 1], 0, 0, startFrame, endFrame, textJustification);
	}
	return true;
}

BOOL seq_ClearTextForVideo(void)
{
	SDWORD i, j;

	for (j=0; j < MAX_SEQ_LIST; j++)
	{
		for(i=0;i<MAX_TEXT_OVERLAYS;i++)
		{
			aSeqList[j].aText[i].pText[0] = 0;
			aSeqList[j].aText[i].x = 0;
			aSeqList[j].aText[i].y = 0;
			aSeqList[j].aText[i].startFrame = 0;
			aSeqList[j].aText[i].endFrame = 0;
			aSeqList[j].aText[i].bSubtitle = 0;
		}
		aSeqList[j].currentText = 0;
	}
	return true;
}

static BOOL seq_AddTextFromFile(const char *pTextName, SEQ_TEXT_POSITIONING textJustification)
{
	char aTextName[MAX_STR_LENGTH];
	char *pTextBuffer, *pCurrentLine, *pText;
	UDWORD fileSize;
	SDWORD xOffset, yOffset, startFrame, endFrame;
	const char *seps = "\n";

	// NOTE: The original game never had a fullscreen mode for FMVs on >640x480 screens.
	// They would just use double sized videos, and move the text to that area.
	// We just use the full screen for text right now, instead of using offsets.
	// However, depending on reaction, we may use the old style again.
	D_H2 = 0;				//( pie_GetVideoBufferHeight()- 480)/2;
	D_W2 = 0;				//( pie_GetVideoBufferWidth() - 640)/2;
	ssprintf(aTextName, "sequenceaudio/%s", pTextName);

	if (loadFileToBufferNoError(aTextName, fileLoadBuffer, FILE_LOAD_BUFFER_SIZE, &fileSize) == false)  //Did I mention this is lame? -Q
	{
		return false;
	}

	pTextBuffer = fileLoadBuffer;
	pCurrentLine = strtok(pTextBuffer,seps);
	while(pCurrentLine != NULL)
	{
		if (*pCurrentLine != '/')
		{
			if (sscanf(pCurrentLine,"%d %d %d %d", &xOffset, &yOffset, &startFrame, &endFrame) == 4)
			{
				// Since all the positioning was hardcoded to specific values, we now calculate the
				// ratio of our screen, compared to what the game expects and multiply that to x, y.
				// This makes the text always take up the full screen, instead of original style.
				xOffset = (double)pie_GetVideoBufferWidth() / 640. * (double)xOffset;
				yOffset = (double)pie_GetVideoBufferHeight() / 480. * (double)yOffset;
				//get the text
				pText = strrchr(pCurrentLine,'"');
				ASSERT(pText != NULL, "error parsing text file");
				if (pText != NULL)
				{
					*pText = (UBYTE)0;
				}
				pText = strchr(pCurrentLine,'"');
				ASSERT(pText != NULL, "error parsing text file");
				if (pText != NULL)
				{
					seq_AddTextForVideo(&pText[1], xOffset, yOffset, startFrame, endFrame, textJustification);
				}
			}
		}
		//get next line
		pCurrentLine = strtok(NULL,seps);
	}
	return true;
}

//clear the sequence list
void seq_ClearSeqList(void)
{
	SDWORD i;

	seq_ClearTextForVideo();
	for(i=0;i<MAX_SEQ_LIST;i++)
	{
		aSeqList[i].pSeq = NULL;
	}
	currentSeq = -1;
	currentPlaySeq = -1;
}

//add a sequence to the list to be played
void seq_AddSeqToList(const char *pSeqName, const char *pAudioName, const char *pTextName, BOOL bLoop)
{
	currentSeq++;

	ASSERT(currentSeq < MAX_SEQ_LIST, "too many sequences");
	if (currentSeq >=  MAX_SEQ_LIST)
	{
		return;
	}

	//OK so add it to the list
	aSeqList[currentSeq].pSeq = pSeqName;
	aSeqList[currentSeq].pAudio = pAudioName;
	aSeqList[currentSeq].bSeqLoop = bLoop;
	if (pTextName != NULL)
	{
		// Ordinary text shouldn't be justified
		seq_AddTextFromFile(pTextName, SEQ_TEXT_POSITION);
	}

	if (bSeqSubtitles)
	{
		char aSubtitleName[MAX_STR_LENGTH];
		size_t check_len = sstrcpy(aSubtitleName, pSeqName);
		char* extension;

		ASSERT(check_len < sizeof(aSubtitleName), "given sequence name (%s) longer (%zu) than buffer (%zu)", pSeqName, check_len, sizeof(aSubtitleName));

		// check for a subtitle file
		extension = strrchr(aSubtitleName, '.');
		if (extension)
			*extension = '\0';
		check_len = sstrcat(aSubtitleName, ".txt");
		ASSERT(check_len < sizeof(aSubtitleName), "sequence name to long to attach an extension too");

		// Subtitles should be center justified
		seq_AddTextFromFile(aSubtitleName, SEQ_TEXT_JUSTIFY);
	}
}

/*checks to see if there are any sequences left in the list to play*/
BOOL seq_AnySeqLeft(void)
{
	UBYTE		nextSeq;

	nextSeq = (UBYTE)(currentPlaySeq+1);

	//check haven't reached end
	if (nextSeq > MAX_SEQ_LIST)
	{
		return false;
	}
	else if (aSeqList[nextSeq].pSeq)
	{
		return true;
	}
	else
	{
		return false;
	}
}

static void seqDispCDOK( void )
{
	BOOL	bPlayedOK;

	if ( bBackDropWasAlreadyUp == false )
	{
		screen_StopBackDrop();
	}

	currentPlaySeq++;
	if (currentPlaySeq >= MAX_SEQ_LIST)
	{
		bPlayedOK = false;
	}
	else
	{
		bPlayedOK = seq_StartFullScreenVideo(aSeqList[currentPlaySeq].pSeq, aSeqList[currentPlaySeq].pAudio, VIDEO_USER_CHOSEN_RESOLUTION);
	}

	if ( bPlayedOK == false )
	{
		//don't do the callback if we're playing the win/lose video
		if (!getScriptWinLoseVideo())
		{
			debug(LOG_SCRIPT, "*** Called video quit trigger!");
			// Not sure this is correct... CHECK, since the callback should ONLY
			// be called when a video is playing (always?)
			if (seq_Playing())
				eventFireCallbackTrigger((TRIGGER_TYPE)CALL_VIDEO_QUIT);
		}
		else
		{
			displayGameOver(getScriptWinLoseVideo() == PLAY_WIN);
		}
	}
}

/*returns the next sequence in the list to play*/
void seq_StartNextFullScreenVideo(void)
{
		seqDispCDOK();
		return;
}

void seq_SetSubtitles(BOOL bNewState)
{
	bSeqSubtitles = bNewState;
}

BOOL seq_GetSubtitles(void)
{
	return bSeqSubtitles;
}
