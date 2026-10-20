/**
 * WiiSX - PlugPAD.c
 * Copyright (C) 2007, 2008, 2009 Mike Slegeir
 * Copyright (C) 2007, 2008, 2009, 2010 sepp256
 * Copyright (C) 2007, 2008, 2009 emu_kidid
 * 
 * Basic Analog PAD plugin for WiiSX
 *
 * WiiSX homepage: http://www.emulatemii.com
 * email address: tehpola@gmail.com
 *                sepp256@gmail.com
 *                emukidid@gmail.com
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
**/

#include <gccore.h>
#include <stdint.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <ogc/pad.h>
#include <libpcsxcore/plugins.h>
#include <libpcsxcore/psxcommon.h>
#include <psemu_plugin_defs.h>
#include "gc_input/controller.h"
#include "wiiSXconfig.h"
#ifdef HW_RVL
#include <wiiuse/wpad.h>
#endif



/* Controller type, later do this by a Variable in the GUI */
long  PadFlags = 0;

virtualControllers_t virtualControllers[4];

controller_t* controller_ts[num_controller_t] =
#if defined(WII) && !defined(NO_BT)
	{ &controller_GC, &controller_Classic,
	  &controller_WiimoteNunchuk,
	  &controller_Wiimote,
	 };
#else
	{ &controller_GC,
	 };
#endif

// Use to invoke func on the mapped controller with args
#define DO_CONTROL(Control,func,args...) \
	virtualControllers[Control].control->func( \
		virtualControllers[Control].number, ## args)

#if defined(WII) && !defined(NO_BT)
/*
 * [Wii 확장 컨트롤러(클래식 등) 즉시 인식 패치]
 *
 * 동작 원리:
 *  - 위모컨이 블루투스로 연결된 직후에는 libogc가 "어떤 확장 컨트롤러가 꽂혀 있는지"를
 *    비동기로 조회한다(수백 ms 소요). 이 조회가 끝나기 전에 WPAD_Probe()를 부르면
 *    클래식 컨트롤러가 아직 'EXP_NONE'(=위모컨 단독)으로 보인다.
 *  - 기존 코드는 시작 시 control_info_init()에서 딱 한 번만 장치를 배정했고,
 *    게임 실행 중에는 다시 확인하지 않아서(메뉴 화면에서만 Gui::draw가 재배정)
 *    홈/리셋 버튼으로 메뉴에 들어가야 클래식이 잡혔다.
 *  - 해결: (1) 시작 시 확장 컨트롤러 감지가 안정될 때까지 잠깐 대기,
 *          (2) 게임 중에도 주기적으로 4개 채널의 상태를 비교해 바뀌면 자동 재배정.
 */

// 4개 채널의 (연결상태, 확장 컨트롤러 종류)를 4비트씩 묶은 값. 값이 바뀌면 장치 구성이 바뀐 것.
static u32 wpad_signature(void){
	u32 sig = 0;
	int i;
	for(i=0; i<4; ++i){
		u32 expType = 0;
		int err = WPAD_Probe(i, &expType);
		// 0: 미연결/준비 안 됨, 1+종류: 연결됨(1=단독, 2=눈챠쿠, 3=클래식 ...)
		sig = (sig << 4) | (err == WPAD_ERR_NONE ? (1 + (expType & 7)) : 0);
	}
	return sig;
}

static u32 wpad_last_sig = 0;

// 시작 시: 확장 컨트롤러 감지 결과가 일정 시간 변하지 않을 때까지 최대 약 1.6초 대기.
static void wait_wpad_settle(void){
	u32 last = (u32)-1;
	int stable = 0, i;
	for(i=0; i<100; ++i){
		u32 sig;
		WPAD_ScanPads();
		sig = wpad_signature();
		if(sig == last) ++stable; else { stable = 0; last = sig; }
		// 연결된 위모컨이 전혀 없으면 기다릴 이유가 없으니 바로 종료
		if(sig == 0 && stable >= 3) break;
		// 약 240ms(15프레임) 동안 변화 없으면 감지 완료로 간주
		if(stable >= 15) break;
		usleep(16000);
	}
	wpad_last_sig = wpad_signature();
}

// 게임 실행 중 호출(프레임마다 불러도 됨): 약 0.5초마다 구성이 바뀌었는지 확인해 재배정.
void control_hotplug_poll(void){
	static int frames = 0;
	u32 sig;
	if(!padAutoAssign) return;          // 수동 배정 모드에서는 사용자 설정 유지
	if(++frames < 30) return;
	frames = 0;
	sig = wpad_signature();
	if(sig == wpad_last_sig) return;
	wpad_last_sig = sig;
	auto_assign_controllers();          // 클래식 우선순위로 다시 배정
}
#else
void control_hotplug_poll(void){ }
#endif

void control_info_init(void){
	//Call once during emulator start to auto assign controllers
#if defined(WII) && !defined(NO_BT)
	wait_wpad_settle();   // 클래식 컨트롤러 감지가 끝난 뒤에 배정해야 바로 인식됨
#endif
	init_controller_ts();
	auto_assign_controllers();
#if defined(WII) && !defined(NO_BT)
	wpad_last_sig = wpad_signature();
#endif
}

void pauseInput(void){
	int i;
	for(i=0; i<4; ++i)
		if(virtualControllers[i].inUse) DO_CONTROL(i, pause);
}

void resumeInput(void){
	int i;
	for(i=0; i<4; ++i)
		if(virtualControllers[i].inUse) DO_CONTROL(i, resume);
}

void init_controller_ts(void){
	int i, j;
	for(i=0; i<num_controller_t; ++i){
		controller_ts[i]->refreshAvailable();

		for(j=0; j<4; ++j){
			memcpy(&controller_ts[i]->config[j],
			       &controller_ts[i]->config_default,
			       sizeof(controller_config_t));
			memcpy(&controller_ts[i]->config_slot[j],
			       &controller_ts[i]->config_default,
			       sizeof(controller_config_t));
		}
	}
}

void assign_controller(int wv, controller_t* type, int wp){
	virtualControllers[wv].control = type;
	virtualControllers[wv].inUse   = 1;
	virtualControllers[wv].number  = wp;
	virtualControllers[wv].config  = &type->config[wv];

	type->assign(wp,wv);
}

void unassign_controller(int wv){
	virtualControllers[wv].control = NULL;
	virtualControllers[wv].inUse   = 0;
	virtualControllers[wv].number  = -1;
}

void auto_assign_controllers(void)
{
	//TODO: Map 5 or 8 controllers if multitaps are used.
	int i,t,w;
	int num_assigned[num_controller_t];

//	init_controller_ts();

	memset(num_assigned, 0, sizeof(num_assigned));

	// Map controllers in the priority given
	// Outer loop: virtual controllers
	for(i=0; i<4; ++i){
		// Middle loop: controller type
		for(t=0; t<num_controller_t; ++t){
			controller_t* type = controller_ts[t];
			type->refreshAvailable();

			// Inner loop: which controller
			for(w=num_assigned[t]; w<4 && !type->available[w]; ++w, ++num_assigned[t]);
			// If we've exhausted this type, move on
			if(w == 4) continue;

			assign_controller(i, type, w);
			padType[i] = type == &controller_GC ? PADTYPE_GAMECUBE : PADTYPE_WII;
			padAssign[i] = w;

			// Don't assign the next type over this one or the same controller
			++num_assigned[t];
			break;
		}
		if(t == num_controller_t)
			break;
	}

	// 'Initialize' the unmapped virtual controllers
	for(; i<4; ++i){
		unassign_controller(i);
		padType[i] = PADTYPE_NONE;
	}
}

int load_configurations(FILE* f, controller_t* controller){
	int i,j;
	char magic[4] = { 
		'W', 'X', controller->identifier, CONTROLLER_CONFIG_VERSION
	};
	char actual[4];
	fread(actual, 1, 4, f);
	if(memcmp(magic, actual, 4))
		return 0;
	
	inline button_t* getPointer(button_t* list, int size){
		int index;
		fread(&index, 4, 1, f);
		return list + (index % size);
	}
	inline button_t* getButton(void){
		return getPointer(controller->buttons, controller->num_buttons);
	}
	
	for(i=0; i<4; ++i){
		controller->config_slot[i].SQU = getButton();
		controller->config_slot[i].CRO = getButton();
		controller->config_slot[i].CIR = getButton();
		controller->config_slot[i].TRI = getButton();
		
		controller->config_slot[i].R1 = getButton();
		controller->config_slot[i].L1 = getButton();
		controller->config_slot[i].R2 = getButton();
		controller->config_slot[i].L2 = getButton();
		controller->config_slot[i].R3 = getButton();
		controller->config_slot[i].L3 = getButton();

		controller->config_slot[i].DL = getButton();
		controller->config_slot[i].DR = getButton();
		controller->config_slot[i].DU = getButton();
		controller->config_slot[i].DD = getButton();

		controller->config_slot[i].START  = getButton();
		controller->config_slot[i].SELECT = getButton();
				
		controller->config_slot[i].analogL = 
			getPointer(controller->analog_sources, controller->num_analog_sources);
		controller->config_slot[i].analogR = 
			getPointer(controller->analog_sources, controller->num_analog_sources);
		controller->config_slot[i].exit =
			getPointer(controller->menu_combos, controller->num_menu_combos);
		fread(&controller->config_slot[i].invertedYL, 4, 1, f);
		fread(&controller->config_slot[i].invertedYR, 4, 1, f);
	}

	if (loadButtonSlot != LOADBUTTON_DEFAULT)
		for(j=0; j<4; ++j)
			memcpy(&controller->config[j],
			       &controller->config_slot[(int)loadButtonSlot],
			       sizeof(controller_config_t));
	
	return 1;
}

void save_configurations(FILE* f, controller_t* controller){
	int i;
	char magic[4] = { 
		'W', 'X', controller->identifier, CONTROLLER_CONFIG_VERSION
	};
	fwrite(magic, 1, 4, f);
	
	for(i=0; i<4; ++i){
		fwrite(&controller->config_slot[i].SQU->index, 4, 1, f);
		fwrite(&controller->config_slot[i].CRO->index, 4, 1, f);
		fwrite(&controller->config_slot[i].CIR->index, 4, 1, f);
		fwrite(&controller->config_slot[i].TRI->index, 4, 1, f);
		
		fwrite(&controller->config_slot[i].R1->index, 4, 1, f);
		fwrite(&controller->config_slot[i].L1->index, 4, 1, f);
		fwrite(&controller->config_slot[i].R2->index, 4, 1, f);
		fwrite(&controller->config_slot[i].L2->index, 4, 1, f);
		fwrite(&controller->config_slot[i].R3->index, 4, 1, f);
		fwrite(&controller->config_slot[i].L3->index, 4, 1, f);
		
		fwrite(&controller->config_slot[i].DL->index, 4, 1, f);
		fwrite(&controller->config_slot[i].DR->index, 4, 1, f);
		fwrite(&controller->config_slot[i].DU->index, 4, 1, f);
		fwrite(&controller->config_slot[i].DD->index, 4, 1, f);
		
		fwrite(&controller->config_slot[i].START->index, 4, 1, f);
		fwrite(&controller->config_slot[i].SELECT->index, 4, 1, f);
				
		fwrite(&controller->config_slot[i].analogL->index, 4, 1, f);
		fwrite(&controller->config_slot[i].analogR->index, 4, 1, f);
		fwrite(&controller->config_slot[i].exit->index, 4, 1, f);
		fwrite(&controller->config_slot[i].invertedYL, 4, 1, f);
		fwrite(&controller->config_slot[i].invertedYR, 4, 1, f);
	}
}

long PAD__init(long flags) {
	PadFlags |= flags;

	return PSE_PAD_ERR_SUCCESS;
}

long PAD__shutdown(void) {
	return PSE_PAD_ERR_SUCCESS;
}

long PAD__open(void)
{
	return PSE_PAD_ERR_SUCCESS;
}

long PAD__close(void) {
	return PSE_PAD_ERR_SUCCESS;
}

