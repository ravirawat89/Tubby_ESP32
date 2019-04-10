/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Attributes State Machine */
enum
{
    IDX_SVC_AQUARIUS,
    IDX_CHAR_BATTERY,
    IDX_CHAR_VAL_BATTERY,
    IDX_CHAR_CURRENT_TIME,
    IDX_CHAR_VAL_CURRENT_TIME,
    IDX_CHAR_COMMAND,
    IDX_CHAR_VAL_COMMAND,
    IDX_CHAR_POTS,
    IDX_CHAR_VAL_POTS,
    IDX_CHAR_NEW_TIME_POINT,
    IDX_CHAR_VAL_NEW_TIME_POINT,
    IDX_CHAR_LOG_EVENT,
    IDX_CHAR_VAL_LOG_EVENT,

    AQUARIUS_IDX_NB,
};
