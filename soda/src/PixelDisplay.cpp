#include "PixelDisplay.h"
#include "SdlUtils.h"
#include "SdlGlue.h"
#include "Color.h"

using namespace MiniScript;
using namespace SdlGlue;

namespace SdlGlue {

PixelDisplay* mainPixelDisplay = nullptr;
static SDL_Renderer* mainRenderer = nullptr;

static int ceilDiv(int x, int y) {
    if (x == 0) return 0;
    return 1 + ((x - 1) / y);
}

static void Swap(int& a, int& b) {
    int temp = a;
    a = b;
    b = temp;
}

static bool IsPointWithinEllipse(float x, float y, SDL_Rect* ellipse) {
    float halfWidth = ellipse->w/2;
    float dx = x - (ellipse->x + halfWidth);
    float term1 = (dx * dx) / (halfWidth * halfWidth);
    
    float halfHeight = ellipse->h/2;
    float dy = y - (ellipse->y + halfHeight);
    float term2 = (dy * dy) / (halfHeight * halfHeight);
    
    return term1 + term2 <= 1;
}

PixelDisplay::PixelDisplay() {
    totalWidth = GetWindowWidth();
    totalHeight = GetWindowHeight();
    drawColor = Color::white;
    AllocArrays();
    Clear();
}

PixelDisplay::~PixelDisplay() {
    DeallocArrays();
}

void SetupPixelDisplay(SDL_Renderer *renderer) {
    mainRenderer = renderer;
    mainPixelDisplay = new PixelDisplay();
}

void ShutdownPixelDisplay() {
    delete mainPixelDisplay;
    mainPixelDisplay = nullptr;
}

void RenderPixelDisplay() {
    mainPixelDisplay->Render();
}

void PixelDisplay::AllocArrays() {
    tileCols = ceilDiv(totalWidth, tileWidth);
    tileRows = ceilDiv(totalHeight, tileHeight);
    int qtyTiles = tileCols * tileRows;
    
    tileTex = new SDL_Texture*[qtyTiles];
    textureInUse = new bool[qtyTiles];
    tileColor = new Color[qtyTiles];
    tileNeedsUpdate = new bool[qtyTiles];
    pixelCache = new CachedPixels[qtyTiles];
    for (int i=0; i<qtyTiles; i++) {
        SDL_Texture *tex = SDL_CreateTexture(mainRenderer, 
            SDL_PIXELFORMAT_RGBA32, 
            SDL_TEXTUREACCESS_STREAMING,
            tileWidth, tileHeight);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        tileTex[i] = tex;
        textureInUse[i] = false;
        tileColor[i] = Color(0,0,0,0);
        tileNeedsUpdate[i] = false;
        pixelCache[i].pixels = nullptr;
    }
}

void PixelDisplay::DeallocArrays() {
    int qtyTiles = tileCols * tileRows;
    for (int i=0; i<qtyTiles; i++) {
        SDL_DestroyTexture(tileTex[i]);
        if (pixelCache[i].pixels) delete[] pixelCache[i].pixels;
    }
    delete[] tileTex;
    delete[] textureInUse;
    delete[] tileColor;
    delete[] tileNeedsUpdate;
    delete[] pixelCache;
}

void PixelDisplay::Clear(Color color) {
    int qtyTiles = tileCols * tileRows;
    for (int i=0; i<qtyTiles; i++) {
        textureInUse[i] = false;
        tileColor[i] = color;
    }
}

void PixelDisplay::Render() {
    int i = 0;
    int windowHeight = tileRows * tileHeight;
    
    for (int row=0; row < tileRows; row++) {
        int yPos = windowHeight - (row + 1) * tileHeight;
        
        for (int col=0; col < tileCols; col++) {
            SDL_Rect destRect = { col*tileWidth, yPos, tileWidth, tileHeight };
            if (textureInUse[i]) {
                if (tileNeedsUpdate[i]) {
                    void* pixels;
                    int pitch;
                    int err = SDL_LockTexture(tileTex[i], NULL, &pixels, &pitch);
                    if (err) {
                        printf("Error in SDL_LockTexture: %s\n", SDL_GetError());
                        continue;
                    }
                    
                    Color* srcP = pixelCache[i].pixels + (tileHeight - 1) * tileWidth;
                    Uint8* destP = (Uint8*)pixels;
                    int bytesToCopy = tileWidth * 4;
                    
                    if (bytesToCopy == pitch) {
                        for (int y = 0; y < tileHeight; y++) {
                            memcpy(destP + y * pitch, srcP - y * tileWidth, bytesToCopy);
                        }
                    } else {
                        for (int y = 0; y < tileHeight; y++) {
                            memcpy(destP, srcP, bytesToCopy);
                            srcP -= tileWidth;
                            destP += pitch;
                        }
                    }
                    
                    SDL_UnlockTexture(tileTex[i]);
                    tileNeedsUpdate[i] = false;
                }
                SDL_RenderCopy(mainRenderer, tileTex[i], NULL, &destRect);
            } else {
                Color c = tileColor[i];
                if (c.a > 0) {
                    SDL_SetRenderDrawColor(mainRenderer, c.r, c.g, c.b, c.a);
                    SDL_RenderFillRect(mainRenderer, &destRect);
                }
            }
            i++;
        }
    }
}

bool PixelDisplay::TileRangeWithin(SDL_Rect *rect, int* tileCol0, int* tileCol1, int* tileRow0, int* tileRow1) {
    bool ok = true;
    int maxTileCol = tileCols - 1;
    *tileCol0 = ceilDiv(rect->x, tileWidth);
    *tileCol1 = ((rect->x + rect->w + 1) / tileWidth) - 1;
    if (*tileCol0 < 0) *tileCol0 = 0;
    else if (*tileCol0 > maxTileCol) ok = false;
    if (*tileCol1 < 0) ok = false;
    else if (*tileCol1 > maxTileCol) *tileCol1 = maxTileCol;
    
    int maxTileRow = tileRows - 1;
    *tileRow0 = ceilDiv(rect->y, tileHeight);
    *tileRow1 = ((rect->y + rect->h + 1) / tileHeight) - 1;
    if (*tileRow0 < 0) *tileRow0 = 0;
    else if (*tileRow0 > maxTileRow) ok = false;
    if (*tileRow1 < 0) ok = false;
    else if (*tileRow1 > maxTileRow) *tileRow1 = maxTileRow;
    
    if (ok) ok = (*tileCol0 <= *tileCol1 && *tileRow0 <= *tileRow1);
    return ok;
}

bool PixelDisplay::EnsureTextureInUse(int tileIndex, Color unlessColor) {
    if (textureInUse[tileIndex]) return true;
    if (tileColor[tileIndex] == unlessColor) return false;
    EnsureTextureInUse(tileIndex);
    return true;
}

void PixelDisplay::EnsureTextureInUse(int tileIndex) {
    if (textureInUse[tileIndex]) return;
    int pixPerTile = tileWidth * tileHeight;
    if (!pixelCache[tileIndex].pixels) pixelCache[tileIndex].pixels = new Color[pixPerTile];
    Color* pixels = pixelCache[tileIndex].pixels;
    Color c = tileColor[tileIndex];
    for (int i=0; i<pixPerTile; i++) *pixels++ = c;
    textureInUse[tileIndex] = true;
}

void PixelDisplay::SetPixel(int x, int y, Color color) {
    if (x < 0 || y < 0 || x >= totalWidth || y >= totalHeight) return;
    int col = x / tileWidth, row = y / tileHeight;
    
    int tileIndex = row * tileCols + col;
    if (!EnsureTextureInUse(tileIndex, color)) return;
    
    int localX = x % tileWidth;
    int localY = y % tileHeight;
    Color* p = pixelCache[tileIndex].pixels + localY*tileWidth + localX;
    if (*p == color) return;
    *p = color;
    tileNeedsUpdate[tileIndex] = true;
}

void PixelDisplay::SetPixelRun(int x0, int x1, int y, Color color) {
    int col = x0 / tileWidth, row = y / tileHeight;
    int localY = y - row*tileHeight;
    int x = x0;
    while (x < x1) {
        int endX = (col+1) * tileWidth;
        if (endX > x1) endX = x1;
        int tileIndex = row * tileCols + col;
        int localX = x % tileWidth;
        if (EnsureTextureInUse(tileIndex, color)) {
            Color* p = pixelCache[tileIndex].pixels + localY*tileWidth + localX;
            for (; x < endX; x++) *p++ = color;
            tileNeedsUpdate[tileIndex] = true;
        }
        col++;
        x = col * tileWidth;
    }
}

void PixelDisplay::DrawLine(int x1, int y1, int x2, int y2, Color color) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    int absDx = dx < 0 ? -dx : dx;
    int absDy = dy < 0 ? -dy : dy;

    bool steep = (absDy > absDx);
    if (steep) {
        Swap(x1, y1);
        Swap(x2, y2);
    }
    
    if (x1 > x2) {
        Swap(x1, x2);
        Swap(y1, y2);
    }

    dx = x2 - x1;
    dy = y2 - y1;
    absDy = dy < 0 ? -dy : dy;

    int error = dx / 2;
    int ystep = (y1 < y2) ? 1 : -1;
    int y = y1;
    
    int maxX = (int)x2;
    
    for (int x=(int)x1; x<=maxX; x++) {
        if (steep) SetPixel(y,x, color);
        else SetPixel(x,y, color);

        error -= absDy;
        if (error < 0) {
            y += ystep;
            error += dx;
        }
    }
}

void PixelDisplay::FillRect(int left, int bottom, int width, int height, Color color) {
    SDL_Rect rect = {left, bottom, width, height};
    
    int y0 = rect.y;
    if (y0 < 0) y0 = 0; else if (y0 >= totalHeight) y0 = totalHeight;
    int y1 = rect.y + rect.h;
    if (y1 < 0) y1 = 0; else if (y1 >= totalHeight) y1 = totalHeight;

    int x0 = rect.x;
    if (x0 < 0) x0 = 0; else if (x0 >= totalWidth) x0 = totalWidth;
    int x1 = rect.x + rect.w;
    if (x1 < 0) x1 = 0; else if (x1 >= totalWidth) x1 = totalWidth;

    int tileCol0, tileCol1, tileRow0, tileRow1;
    if (TileRangeWithin(&rect, &tileCol0, &tileCol1, &tileRow0, &tileRow1)) {
        for (int tileRow = tileRow0; tileRow <= tileRow1; tileRow++) {
            for (int tileCol = tileCol0; tileCol <= tileCol1; tileCol++) {
                int tileIndex = tileRow * tileCols + tileCol;
                textureInUse[tileIndex] = false;
                tileColor[tileIndex] = color;
            }
        }
    }

    for (int y=y0; y<y1; y++) SetPixelRun(x0, x1, y, color);
}

bool PixelDisplay::IsTileWithinEllipse(int col, int row, SDL_Rect* ellipse) {
    int x = col * tileWidth;
    int y = row * tileHeight;
    return IsPointWithinEllipse(x, y, ellipse)
        && IsPointWithinEllipse(x + tileWidth, y, ellipse)
        && IsPointWithinEllipse(x + tileWidth, y + tileHeight, ellipse)
        && IsPointWithinEllipse(x, y + tileHeight, ellipse);
}

void PixelDisplay::FillEllipse(int left, int bottom, int width, int height, Color color) {
    SDL_Rect rect = {left, bottom, width, height};
    if (rect.w <= 2 || rect.h <= 2) {
        FillRect(left, bottom, width, height, color);
        return;
    }

    int y0 = rect.y;
    if (y0 < 0) y0 = 0; else if (y0 >= totalHeight) y0 = totalHeight-1;
    int y1 = rect.y + rect.h;
    if (y1 < 0) y1 = 0; else if (y1 >= totalHeight) y1 = totalHeight;
    
    int tileCol0, tileCol1, tileRow0, tileRow1;
    if (TileRangeWithin(&rect, &tileCol0, &tileCol1, &tileRow0, &tileRow1)) {
        for (int tileRow = tileRow0; tileRow <= tileRow1; tileRow++) {
            for (int tileCol = tileCol0; tileCol <= tileCol1; tileCol++) {
                if (IsTileWithinEllipse(tileCol, tileRow, &rect)) {
                    int tileIndex = tileRow * tileCols + tileCol;
                    textureInUse[tileIndex] = false;
                    tileColor[tileIndex] = color;                    
                }
            }
        }
    }
    
    float r = rect.h * 0.5f;
    float rsqr = r*r;
    float aspect = (float)rect.w / rect.h;
    float rectCenterX = rect.x + rect.w * 0.5f;
    float rectCenterY = rect.y + rect.h * 0.5f;
    for (int y=y0; y<y1; y++) {
        float cy = rectCenterY - y - 0.5f;
        float cx = sqrt(rsqr - cy*cy) * aspect;
        int x0 = (rectCenterX - cx + 0.5f);
        if (x0 < 0) x0 = 0; else if (x0 >= totalWidth) x0 = totalWidth;
        int x1 = (rectCenterX + cx + 0.5f);
        if (x1 < 0) x1 = 0; else if (x1 >= totalWidth) x1 = totalWidth;
        SetPixelRun(x0, x1, y, color);
    }
}

} // namespace SdlGlue
