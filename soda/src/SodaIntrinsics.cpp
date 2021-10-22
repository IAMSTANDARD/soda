//
//  SodaIntrinsics.cpp
//  This module adds all the MiniScript APIs that users use to interface with Soda
//	to create games.  That includes defining intrinsic classes like Display, Sound,
//	file, etc.
//
//  Created by Joe Strout on 7/29/21.
//

#include "SodaIntrinsics.h"
#include "SdlGlue.h"
#include "TextDisplay.h"
#include "SdlAudio.h"
#include "MiniScript/String.h"
#include "MiniScript/UnicodeUtil.h"
#include "MiniScript/UnitTest.h"
#include "MiniScript/SimpleVector.h"
#include "MiniScript/List.h"
#include "MiniScript/Dictionary.h"
#include "MiniScript/MiniscriptParser.h"
#include "MiniScript/MiniscriptInterpreter.h"
#include "OstreamSupport.h"
#include "MiniScript/SplitJoin.h"
#include "BoundingBox.h"
#include "Sprite.h"

using namespace MiniScript;

Value spriteList = ValueList();
Value white("#FFFFFF");
Value xStr("x");
Value yStr("y");
Value widthStr("width");
Value heightStr("height");
Value rotationStr("rotation");
Value scaleStr("scale");

static IntrinsicResult intrinsic_clear(Context *context, IntrinsicResult partialResult) {
	SdlGlue::Clear();
	return IntrinsicResult::Null;
}

static IntrinsicResult intrinsic_sprites(Context *context, IntrinsicResult partialResult) {
	return IntrinsicResult(spriteList);
}

//--------------------------------------------------------------------------------
// Bounds class
//--------------------------------------------------------------------------------

// BoundingBoxStorage: wraps and reference-counts a BoundingBox;
// used as the _handle of a Bounds object in MiniScript.
class BoundingBoxStorage : public RefCountedStorage {
public:
	BoundingBoxStorage(BoundingBox *b) : boundingBox(b) {}
	
	virtual ~BoundingBoxStorage() {
		delete boundingBox;
		boundingBox = nullptr;
	}
	
	BoundingBox *boundingBox;
};

ValueDict boundsClass;
static Intrinsic *i_bounds_corners = nullptr;
static Intrinsic *i_bounds_overlaps = nullptr;
static Intrinsic *i_bounds_contains = nullptr;

static IntrinsicResult intrinsic_boundsClass(Context *context, IntrinsicResult partialResult) {
	return IntrinsicResult(boundsClass);
}

static bool boundsAssignOverride(ValueDict& boundsMap, Value key, Value value) {
	// If the value hasn't changed, do nothing.
	Value curVal = boundsMap.Lookup(key, Value::null);
	if (curVal == value) return not value.IsNull();	// (block the assignment, unless actually assigning null)
	
	// To avoid doing a bunch of string comparisons, we'll just assume
	// (with high probability) that any assignment to a bounds should
	// trigger recomputing the bounding box.
	MiniScript::Value handle = boundsMap.Lookup(SdlGlue::magicHandle, Value::null);
	BoundingBoxStorage *storage = nullptr;
	if (handle.type == ValueType::Handle) {
		// ToDo: how do we be sure the data is specifically a BoundingBoxStorage?
		// Do we need to enable RTTI, or use some common base class?
		storage = ((BoundingBoxStorage*)(handle.data.ref));
	}
	if (storage) storage->boundingBox->dirty = true;
	// Marking it dirty in this way will signal us (in BoundingBoxFromMap) that
	// we need to copy values out of the map and apply to the box before we
	// do any actual computations with it.
	
	return false;	// allow the assignment
}

static BoundingBox* BoundingBoxFromMap(Value map) {
	if (map.type != ValueType::Map) return nullptr;
	
	MiniScript::Value handle = map.Lookup(SdlGlue::magicHandle);
	BoundingBoxStorage *storage = nullptr;
	if (handle.type == ValueType::Handle) {
		// ToDo: how do we be sure the data is specifically a SoundStorage?
		// Do we need to enable RTTI, or use some common base class?
		storage = ((BoundingBoxStorage*)(handle.data.ref));
	}
	if (storage == NULL || storage->boundingBox == nullptr) {
		Value x = map.Lookup(xStr);
		Value y = map.Lookup(yStr);
		Value width = map.Lookup(widthStr);
		Value height = map.Lookup(heightStr);
		Value rotation = map.Lookup(rotationStr);
		BoundingBox *bb = new BoundingBox(Vector2(x.DoubleValue(), y.DoubleValue()),
										  Vector2(width.DoubleValue()/2, height.DoubleValue()/2),
										  rotation.DoubleValue() * 57.29578);
		handle = Value::NewHandle(new BoundingBoxStorage(bb));
		map.SetElem(SdlGlue::magicHandle, handle);
		map.GetDict().SetAssignOverride(boundsAssignOverride);
		return bb;
	} else {
		BoundingBox *bb = storage->boundingBox;
		if (bb->dirty) {
			bb->center.x = map.Lookup(xStr).DoubleValue();
			bb->center.y = map.Lookup(yStr).DoubleValue();
			bb->halfSize.x = map.Lookup(widthStr).DoubleValue()/2;
			bb->halfSize.y = map.Lookup(widthStr).DoubleValue()/2;
			bb->rotation = map.Lookup(rotationStr).DoubleValue() * 57.29578;
//			printf("Freshened BB with center %lf,%lf, halfSize %lf,%lf, rotation %lf\n",
//				   bb->center.x, bb->center.y, bb->halfSize.x, bb->halfSize.y, bb->rotation);
			bb->Freshen();
		}
		return bb;
	}
}

static IntrinsicResult intrinsic_boundsCorners(Context *context, IntrinsicResult partialResult) {
	Value self = context->GetVar("self");
	
	return IntrinsicResult::Null;
}

// Helper method to get X and Y parameters that may be passed in any of several ways:
//  1. two separate "x" and "y" parameters
//  2. an [x, y] list
//	3. a may with "x" and "y" entries
static void GetXYParameters(Context *context, double* outX, double* outY) {
	Value p1 = context->GetVar("x");
	if (p1.type == ValueType::Map) {
		*outX = p1.Lookup(xStr).DoubleValue();
		*outY = p1.Lookup(yStr).DoubleValue();
	} else if (p1.type == ValueType::List) {
		ValueList list = p1.GetList();
		*outX = list[0].DoubleValue();
		*outY = list[1].DoubleValue();
	} else {
		*outX = p1.DoubleValue();
		*outY = context->GetVar("y").DoubleValue();
	}
}

static IntrinsicResult intrinsic_boundsContains(Context *context, IntrinsicResult partialResult) {
	Value self = context->GetVar("self");
	double x, y;
	GetXYParameters(context, &x, &y);
	
	BoundingBox* bb = BoundingBoxFromMap(self);
	if (bb == nullptr) return IntrinsicResult::Null;
	Value result = Value::Truth(bb->Contains(Vector2(x, y)));
	return IntrinsicResult(result);
}

static IntrinsicResult intrinsic_boundsOverlaps(Context *context, IntrinsicResult partialResult) {
	Value self = context->GetVar("self");
	Value other = context->GetVar("other");
	
	BoundingBox* bb = BoundingBoxFromMap(self);
	if (bb == nullptr) return IntrinsicResult::Null;
	BoundingBox* bb2 = BoundingBoxFromMap(other);
	if (bb2 == nullptr) return IntrinsicResult::Null;
	Value result = Value::Truth(bb->Intersects(*bb2));
	return IntrinsicResult(result);
}

//--------------------------------------------------------------------------------
// Image class
//--------------------------------------------------------------------------------
ValueDict imageClass;
static Intrinsic *i_image_getImage = nullptr;
static Intrinsic *i_image_pixel = nullptr;
static Intrinsic *i_image_setPixel = nullptr;

static IntrinsicResult intrinsic_imageClass(Context *context, IntrinsicResult partialResult) {
	return IntrinsicResult(imageClass);
}

static IntrinsicResult intrinsic_image_pixel(Context *context, IntrinsicResult partialResult) {
	Value self = context->GetVar("self");
	Value x = context->GetVar("x");
	Value y = context->GetVar("y");
	return IntrinsicResult(SdlGlue::GetImagePixel(self, (int)x.IntValue(), (int)x.IntValue()));
}

static IntrinsicResult intrinsic_image_setPixel(Context *context, IntrinsicResult partialResult) {
	Value self = context->GetVar("self");
	Value x = context->GetVar("x");
	Value y = context->GetVar("y");
	Value color = context->GetVar("color");
	SdlGlue::SetImagePixel(self, (int)x.IntValue(), (int)y.IntValue(), color.ToString());
	return IntrinsicResult::Null;
}

static IntrinsicResult intrinsic_image_getImage(Context *context, IntrinsicResult partialResult) {
	Value self = context->GetVar("self");
	Value left = context->GetVar("left");
	Value bottom = context->GetVar("bottom");
	Value width = context->GetVar("width");
	Value height = context->GetVar("height");
	return IntrinsicResult(SdlGlue::GetSubImage(self,
		(int)left.IntValue(), (int)bottom.IntValue(), (int)width.IntValue(), (int)height.IntValue()));
}

//--------------------------------------------------------------------------------
// key module
//--------------------------------------------------------------------------------
static Intrinsic *i_key_pressed = nullptr;
static Intrinsic *i_key_axis = nullptr;

static IntrinsicResult intrinsic_keyModule(Context *context, IntrinsicResult partialResult) {
	static ValueDict keyModule;
	
	if (keyModule.Count() == 0) {
		keyModule.SetValue("pressed", i_key_pressed->GetFunc());
		keyModule.SetValue("axis", i_key_axis->GetFunc());
	}
	
	return IntrinsicResult(keyModule);
}

static IntrinsicResult intrinsic_key_pressed(Context *context, IntrinsicResult partialResult) {
	Value keyName = context->GetVar("keyName");
	if (keyName.IsNull()) return IntrinsicResult::Null;
	return IntrinsicResult(SdlGlue::IsKeyPressed(keyName.ToString()));
}

static IntrinsicResult intrinsic_key_axis(Context *context, IntrinsicResult partialResult) {
	Value keyName = context->GetVar("axisName");
	if (keyName.IsNull()) return IntrinsicResult::Null;
	return IntrinsicResult(SdlGlue::GetAxis(keyName.ToString()));
}

//--------------------------------------------------------------------------------
// mouse module
//--------------------------------------------------------------------------------

static Intrinsic *i_mouse_button = nullptr;
ValueDict mouseModule;

static IntrinsicResult intrinsic_mouseModule(Context *context, IntrinsicResult partialResult) {
	if (mouseModule.Count() < 3) {
		mouseModule.SetValue("button", i_mouse_button->GetFunc());
		// Note: mouse x and y are updated in SdlGlue::Service, so they are
		// ordinary numbers and can be used like any other XY map.
		mouseModule.SetValue("x", SdlGlue::GetMouseX());
		mouseModule.SetValue("y", SdlGlue::GetMouseY());
	}
	
	return IntrinsicResult(mouseModule);
}

static IntrinsicResult intrinsic_mouse_button(Context *context, IntrinsicResult partialResult) {
	Value which = context->GetVar("which");
	if (which.IsNull()) return IntrinsicResult::Null;
	return IntrinsicResult(SdlGlue::IsMouseButtonPressed((int)which.IntValue()));
}


//--------------------------------------------------------------------------------
// Sound class
//--------------------------------------------------------------------------------
ValueDict soundClass;
static Intrinsic *i_sound_play = nullptr;
static Intrinsic *i_sound_stop = nullptr;
static Intrinsic *i_sound_stopAll = nullptr;

static IntrinsicResult intrinsic_soundClass(Context *context, IntrinsicResult partialResult) {
	return IntrinsicResult(soundClass);
}

static IntrinsicResult intrinsic_sound_play(Context *context, IntrinsicResult partialResult) {
	Value self = context->GetVar("self");
	double volume = context->GetVar("volume").DoubleValue();
	double pan = context->GetVar("pan").DoubleValue();
	double speed = context->GetVar("speed").DoubleValue();
	SdlGlue::PlaySound(self, volume, pan, speed);
	return IntrinsicResult::Null;
}

static IntrinsicResult intrinsic_sound_stop(Context *context, IntrinsicResult partialResult) {
	Value self = context->GetVar("self");
	SdlGlue::StopSound(self);
	return IntrinsicResult::Null;
}

static IntrinsicResult intrinsic_sound_stopAll(Context *context, IntrinsicResult partialResult) {
	SdlGlue::StopAllSounds();
	return IntrinsicResult::Null;
}

//--------------------------------------------------------------------------------
// Sprite class
//--------------------------------------------------------------------------------
ValueDict spriteClass;
static Intrinsic *i_sprite_worldBounds = nullptr;
static Intrinsic *i_sprite_contains = nullptr;
static Intrinsic *i_sprite_overlaps = nullptr;

static Value GetWorldBounds(Value sprite) {
	Value localBounds = sprite.Lookup("localBounds");
	BoundingBox *localBbox = BoundingBoxFromMap(localBounds);
	if (!localBbox) return Value::null;
	
	// We may have an up to date world bounds already stored on the sprite.
	// Or we may not.  So start by determining which is the case.
	SpriteHandleData *data = GetSpriteHandleData(sprite);
	if (!data->boundsChanged && data->lastLocalChangeCounter == localBbox->changeCounter && !data->worldBounds.IsNull()) {
		// OK, we have a world bounds, our boundsChange flag hasn't been set, and our local bounds
		// change counter hasn't changed since we last updated it... so the world bounds looks good.
		return data->worldBounds;
	}
	// Something's changed, so we need to recompute our world bounds.
	if (data->worldBounds.IsNull()) {
		ValueDict inst;
		inst.SetValue(Value::magicIsA, boundsClass);
		data->worldBounds = Value(inst);
	}
	data->worldBounds.SetElem(xStr, data->x + localBbox->center.x);
	data->worldBounds.SetElem(yStr, data->y + localBbox->center.y);
	data->worldBounds.SetElem(widthStr, data->scale * localBbox->halfSize.x * 2);
	data->worldBounds.SetElem(heightStr, data->scale * localBbox->halfSize.y * 2);
	data->worldBounds.SetElem(rotationStr, data->rotation + localBbox->rotation);
	return data->worldBounds;
}

static IntrinsicResult intrinsic_sprite_worldBounds(Context *context, IntrinsicResult partialResult) {
	Value self = context->GetVar("self");
	if (self.type != ValueType::Map) return IntrinsicResult::Null;
	
	Value worldBounds = GetWorldBounds(self);
	if (worldBounds.IsNull()) return IntrinsicResult::Null;
	return IntrinsicResult(worldBounds);
}

static IntrinsicResult intrinsic_sprite_contains(Context *context, IntrinsicResult partialResult) {
	// call the normal worldBounds method to get the world bounds of this sprite
	IntrinsicResult r = intrinsic_sprite_worldBounds(context, IntrinsicResult::Null);
	Value worldBounds = r.Result();
	
	// then check the point against that
	if (worldBounds.IsNull()) return IntrinsicResult(Value::zero);

	double x, y;
	GetXYParameters(context, &x, &y);
	BoundingBox* bb = BoundingBoxFromMap(worldBounds);
	if (bb == nullptr) return IntrinsicResult::Null;
	Value result = Value::Truth(bb->Contains(Vector2(x, y)));
	return IntrinsicResult(result);
}

static IntrinsicResult intrinsic_sprite_overlaps(Context *context, IntrinsicResult partialResult) {
	Value self = context->GetVar("self");
	if (self.IsNull()) RuntimeException("Sprite required for self parameter").raise();
	Value myWorldBounds = GetWorldBounds(self);
	if (myWorldBounds.IsNull()) return IntrinsicResult(Value::zero);
	BoundingBox *bb = BoundingBoxFromMap(myWorldBounds);
	
	// ...and the other sprite or bounds
	Value other = context->GetVar("other");
	BoundingBox* bb2 = nullptr;
	if (other.IsA(spriteClass, context->vm)) {
		bb2 = BoundingBoxFromMap(GetWorldBounds(other));
	} else if (other.IsA(boundsClass, context->vm)) {
		bb2 = BoundingBoxFromMap(other);
	} else {
		RuntimeException("Sprite or Bounds required for other parameter").raise();
	}
	if (!bb2) return IntrinsicResult(Value::zero);
	
	Value result = Value::Truth(bb->Intersects(*bb2));
	return IntrinsicResult(result);
}

static IntrinsicResult intrinsic_spriteClass(Context *context, IntrinsicResult partialResult) {
	if (spriteClass.Count() == 0) {
		spriteClass.SetValue("image", Value::null);
		spriteClass.SetValue("x", Value::zero);
		spriteClass.SetValue("y", Value::zero);
		spriteClass.SetValue("scale", Value::one);
		spriteClass.SetValue("rotation", Value::zero);
		spriteClass.SetValue("tint", white);
		spriteClass.SetValue("localBounds", Value::null);
		spriteClass.SetValue("_handle", Value::null);

		i_sprite_worldBounds = Intrinsic::Create("");
		i_sprite_worldBounds->code = &intrinsic_sprite_worldBounds;
		spriteClass.SetValue("worldBounds", i_sprite_worldBounds->GetFunc());

		i_sprite_contains = Intrinsic::Create("");
		i_sprite_contains->AddParam("x", Value::zero);
		i_sprite_contains->AddParam("y", Value::zero);
		i_sprite_contains->code = &intrinsic_sprite_contains;
		spriteClass.SetValue("contains", i_sprite_contains->GetFunc());
		
		i_sprite_overlaps = Intrinsic::Create("");
		i_sprite_overlaps->AddParam("other");
		i_sprite_overlaps->code = &intrinsic_sprite_overlaps;
		spriteClass.SetValue("overlaps", i_sprite_overlaps->GetFunc());
	}
	
	return IntrinsicResult(spriteClass);
}

//--------------------------------------------------------------------------------
// TextDisplay class
//--------------------------------------------------------------------------------
ValueDict textDisplayClassMap;
Value textDisplayClass(textDisplayClassMap);
static Intrinsic *i_textDisplay_clear = nullptr;

static IntrinsicResult intrinsic_textDisplayClass(Context *context, IntrinsicResult partialResult) {
	return IntrinsicResult(textDisplayClass);
}

static IntrinsicResult intrinsic_textDisplay_clear(Context *context, IntrinsicResult partialResult) {
	//Value self = context->GetVar("self");
	// Note: for now, we'll just always access the main text display.
	// When we support multiple text displays, we'll need to be more discriminating.
	SdlGlue::mainTextDisplay->Clear();
	return IntrinsicResult::Null;
}

Value textDisplayInstance;
static IntrinsicResult intrinsic_textDisplayInstance(Context *context, IntrinsicResult partialResult) {
	if (textDisplayInstance.type != ValueType::Map) {
		ValueDict text;
		text.SetValue(Value::magicIsA, textDisplayClass);
		textDisplayInstance = text;
	}
	return IntrinsicResult(textDisplayInstance);
}

//--------------------------------------------------------------------------------
// window module
//--------------------------------------------------------------------------------

static bool windowModuleAssignOverride(ValueDict& windowModule, Value key, Value value) {
	String keystr = key.ToString();
	if (keystr == "width") {
		SdlGlue::SetWindowWidth((int)value.IntValue());
	} else if (keystr == "height") {
		SdlGlue::SetWindowHeight((int)value.IntValue());
	} else if (keystr == "fullScreen") {
		SdlGlue::SetFullScreen(value.BoolValue());
	} else if (keystr == "backColor") {
		SdlGlue::SetBackgroundColor(value.ToString());
		windowModule.SetValue("backColor", SdlGlue::GetBackgroundColor());
	} else {
		return false;		// allow other assignments, why not?
	}
	windowModule.SetValue("width", SdlGlue::GetWindowWidth());
	windowModule.SetValue("height", SdlGlue::GetWindowHeight());
	windowModule.SetValue("fullScreen", SdlGlue::GetFullScreen());
	return true;
}

static IntrinsicResult intrinsic_windowModule(Context *context, IntrinsicResult partialResult) {
	static ValueDict windowModule;
	
	if (windowModule.Count() == 0) {
		windowModule.SetValue("width", SdlGlue::GetWindowWidth());
		windowModule.SetValue("height", SdlGlue::GetWindowHeight());
		windowModule.SetValue("fullScreen", SdlGlue::GetFullScreen());
		windowModule.SetValue("backColor", SdlGlue::GetBackgroundColor());
	}
	
	windowModule.SetAssignOverride(windowModuleAssignOverride);
	
	return IntrinsicResult(windowModule);
}

//--------------------------------------------------------------------------------
// file module additions
//--------------------------------------------------------------------------------

static Intrinsic *i_file_loadImage = nullptr;
static Intrinsic *i_file_loadSound = nullptr;
static Intrinsic *i_file_loadFont = nullptr;

static IntrinsicResult intrinsic_file_loadImage(Context *context, IntrinsicResult partialResult) {
	Value path = context->GetVar("path");
	return IntrinsicResult(SdlGlue::LoadImage(path.ToString()));
}

static IntrinsicResult intrinsic_file_loadSound(Context *context, IntrinsicResult partialResult) {
	Value path = context->GetVar("path");
	return IntrinsicResult(SdlGlue::LoadSound(path.ToString()));
}


//--------------------------------------------------------------------------------
void AddSodaIntrinsics() {
	printf("Adding Soda intrinsics\n");
	Intrinsic *f;

	f = Intrinsic::Create("clear");
	f->code = &intrinsic_clear;

	f = Intrinsic::Create("sprites");	// ToDo: put this in a SpriteDisplay
	f->code = &intrinsic_sprites;
	
	i_bounds_corners = Intrinsic::Create("");
	i_bounds_corners->code = &intrinsic_boundsCorners;
	
	i_bounds_contains = Intrinsic::Create("");
	i_bounds_contains->AddParam("x", Value::zero);
	i_bounds_contains->AddParam("y", Value::zero);
	i_bounds_contains->code = &intrinsic_boundsContains;
	
	i_bounds_overlaps = Intrinsic::Create("");
	i_bounds_overlaps->AddParam("other");
	i_bounds_overlaps->code = &intrinsic_boundsOverlaps;
	
	f = Intrinsic::Create("Bounds");
	f->code = &intrinsic_boundsClass;
	boundsClass.SetValue("x", Value::zero);
	boundsClass.SetValue("y", Value::zero);
	Value v100(100);
	boundsClass.SetValue("width", v100);
	boundsClass.SetValue("height", v100);
	boundsClass.SetValue("rotation", Value::zero);
	boundsClass.SetValue("corners", i_bounds_corners->GetFunc());
	boundsClass.SetValue("contains", i_bounds_contains->GetFunc());
	boundsClass.SetValue("overlaps", i_bounds_overlaps->GetFunc());

	i_image_pixel = Intrinsic::Create("");
	i_image_pixel->AddParam("x", Value::zero);
	i_image_pixel->AddParam("y", Value::zero);
	i_image_pixel->code = &intrinsic_image_pixel;

	i_image_setPixel = Intrinsic::Create("");
	i_image_setPixel->AddParam("x", Value::zero);
	i_image_setPixel->AddParam("y", Value::zero);
	i_image_setPixel->AddParam("color");
	i_image_setPixel->code = &intrinsic_image_setPixel;

	i_image_getImage = Intrinsic::Create("");
	i_image_getImage->AddParam("left", Value::zero);
	i_image_getImage->AddParam("bottom", Value::zero);
	i_image_getImage->AddParam("width", Value(-1));
	i_image_getImage->AddParam("height", Value(-1));
	i_image_getImage->code = &intrinsic_image_getImage;

	f = Intrinsic::Create("Image");
	f->code = &intrinsic_imageClass;
	imageClass.SetValue("width", Value::zero);
	imageClass.SetValue("height", Value::zero);
	imageClass.SetValue("getImage", i_image_getImage->GetFunc());
	imageClass.SetValue("pixel", i_image_pixel->GetFunc());
	imageClass.SetValue("setPixel", i_image_setPixel->GetFunc());

	Intrinsic *fileIntrinsic = Intrinsic::GetByName("file");
	if (fileIntrinsic == nullptr) {
		printf("ERROR: fileModule not found.\nAddSodaIntrinsics must be called after AddShellIntrinsics.\n");
	} else {
		IntrinsicResult result;
		ValueDict fileModule = fileIntrinsic->code(nullptr, result).Result().GetDict();
		
		i_file_loadImage = Intrinsic::Create("");
		i_file_loadImage->AddParam("path", Value::emptyString);
		i_file_loadImage->code = &intrinsic_file_loadImage;
		fileModule.SetValue("loadImage", i_file_loadImage->GetFunc());
		
		i_file_loadSound = Intrinsic::Create("");
		i_file_loadSound->AddParam("path", Value::emptyString);
		i_file_loadSound->code = &intrinsic_file_loadSound;
		fileModule.SetValue("loadSound", i_file_loadSound->GetFunc());
	}
	
	f = Intrinsic::Create("Sprite");
	f->code = &intrinsic_spriteClass;

	i_sound_play = Intrinsic::Create("");
	i_sound_play->AddParam("volume", Value::one);
	i_sound_play->AddParam("pan", Value::zero);
	i_sound_play->AddParam("speed", Value::one);
	i_sound_play->code = &intrinsic_sound_play;
	soundClass.SetValue("play", i_sound_play->GetFunc());

	i_sound_stop = Intrinsic::Create("");
	i_sound_stop->code = &intrinsic_sound_stop;
	soundClass.SetValue("stop", i_sound_stop->GetFunc());

	i_sound_stopAll = Intrinsic::Create("");
	i_sound_stopAll->code = &intrinsic_sound_stopAll;
	soundClass.SetValue("stopAll", i_sound_stopAll->GetFunc());

	f = Intrinsic::Create("Sound");
	f->code = &intrinsic_soundClass;
	soundClass.SetValue("_handle", Value::null);
	soundClass.SetValue("loop", Value::zero);

	i_textDisplay_clear = Intrinsic::Create("");
	i_textDisplay_clear->code = &intrinsic_textDisplay_clear;
	textDisplayClassMap.SetValue("clear", i_textDisplay_clear->GetFunc());
	
	textDisplayClassMap.SetValue("row", Value::zero);
	textDisplayClassMap.SetValue("column", Value::zero);

	f = Intrinsic::Create("TextDisplay");
	f->code = &intrinsic_textDisplayClass;
	
	f = Intrinsic::Create("text");
	f->code = &intrinsic_textDisplayInstance;
	
	f = Intrinsic::Create("key");
	f->code = &intrinsic_keyModule;

	i_key_pressed = Intrinsic::Create("");
	i_key_pressed->AddParam("keyName");
	i_key_pressed->code = &intrinsic_key_pressed;
	
	i_key_axis = Intrinsic::Create("");
	i_key_axis->AddParam("axisName");
	i_key_axis->code = &intrinsic_key_axis;
	
	f = Intrinsic::Create("mouse");
	f->code = &intrinsic_mouseModule;
	
	i_mouse_button = Intrinsic::Create("");
	i_mouse_button->AddParam("which", Value::zero);
	i_mouse_button->code = &intrinsic_mouse_button;

	f = Intrinsic::Create("window");
	f->code = &intrinsic_windowModule;
	

}
