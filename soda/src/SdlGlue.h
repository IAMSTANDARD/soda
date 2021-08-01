//
//  SdlGlue.h
//	This module forms the interface between SDL and the rest of Soda.  It handles all
//	the low-level SDL stuff (except for things complex enough to be split out into
//	their own module, such as audio).
//
//  Created by Joe Strout on 7/29/21.
//

#ifndef SDLGLUE_H
#define SDLGLUE_H

#include <stdio.h>
#include "MiniScript/String.h"
#include "MiniScriptTypes.h"

namespace SdlGlue {

void Setup();
void Service();
void Shutdown();

void DoSdlTest();
bool IsKeyPressed(MiniScript::String keyName);
bool IsMouseButtonPressed(int buttonNum);
int GetMouseX();
int GetMouseY();
MiniScript::Value LoadImage(MiniScript::String path);

// flag set to true when the user tries to quit the app (by closing the window, cmd-Q, etc.)
extern bool quit;

}

#endif /* SDLGLUE_H */
