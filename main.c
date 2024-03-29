#include <time.h>
#include <stdio.h>
#include "raylib.h"
#include "config.h"

#define mcollide(x, y, w, h) \
    CheckCollisionPointRec(GetMousePosition(), (Rectangle){x, y, w, h})

#define lmbdown IsMouseButtonDown(MOUSE_LEFT_BUTTON)
#define lmbup IsMouseButtonReleased(MOUSE_LEFT_BUTTON)
#define focused(i) (i >= WINDOW_LIMIT - 1)

#define RENDER_WIDTH (SCREEN_WIDTH / SCALE)
#define RENDER_HEIGHT (SCREEN_HEIGHT / SCALE)

// _____________________________________________________________________________
//
// DrawTextRec was removed from raylib in 4.0, we need to re-implement it
// https://github.com/raysan5/raylib/blob/master/examples/text/text_rectangle_bounds.c
// _____________________________________________________________________________
//

// Draw text using font inside rectangle limits with support for text selection
static void DrawTextBoxedSelectable(Font font, const char *text, Rectangle rec, float fontSize, float spacing, bool wordWrap, Color tint, int selectStart, int selectLength, Color selectTint, Color selectBackTint)
{
    int length = TextLength(text);  // Total length in bytes of the text, scanned by codepoints in loop

    float textOffsetY = 0;          // Offset between lines (on line break '\n')
    float textOffsetX = 0.0f;       // Offset X to next character to draw

    float scaleFactor = fontSize/(float)font.baseSize;     // Character rectangle scaling factor

    // Word/character wrapping mechanism variables
    enum { MEASURE_STATE = 0, DRAW_STATE = 1 };
    int state = wordWrap? MEASURE_STATE : DRAW_STATE;

    int startLine = -1;         // Index where to begin drawing (where a line begins)
    int endLine = -1;           // Index where to stop drawing (where a line ends)
    int lastk = -1;             // Holds last value of the character position

    for (int i = 0, k = 0; i < length; i++, k++)
    {
        // Get next codepoint from byte string and glyph index in font
        int codepointByteCount = 0;
        int codepoint = GetCodepoint(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, codepoint);

        // NOTE: Normally we exit the decoding sequence as soon as a bad byte is found (and return 0x3f)
        // but we need to draw all of the bad bytes using the '?' symbol moving one byte
        if (codepoint == 0x3f) codepointByteCount = 1;
        i += (codepointByteCount - 1);

        float glyphWidth = 0;
        if (codepoint != '\n')
        {
            glyphWidth = (font.glyphs[index].advanceX == 0) ? font.recs[index].width*scaleFactor : font.glyphs[index].advanceX*scaleFactor;

            if (i + 1 < length) glyphWidth = glyphWidth + spacing;
        }

        // NOTE: When wordWrap is ON we first measure how much of the text we can draw before going outside of the rec container
        // We store this info in startLine and endLine, then we change states, draw the text between those two variables
        // and change states again and again recursively until the end of the text (or until we get outside of the container).
        // When wordWrap is OFF we don't need the measure state so we go to the drawing state immediately
        // and begin drawing on the next line before we can get outside the container.
        if (state == MEASURE_STATE)
        {
            // TODO: There are multiple types of spaces in UNICODE, maybe it's a good idea to add support for more
            // Ref: http://jkorpela.fi/chars/spaces.html
            if ((codepoint == ' ') || (codepoint == '\t') || (codepoint == '\n')) endLine = i;

            if ((textOffsetX + glyphWidth) > rec.width)
            {
                endLine = (endLine < 1)? i : endLine;
                if (i == endLine) endLine -= codepointByteCount;
                if ((startLine + codepointByteCount) == endLine) endLine = (i - codepointByteCount);

                state = !state;
            }
            else if ((i + 1) == length)
            {
                endLine = i;
                state = !state;
            }
            else if (codepoint == '\n') state = !state;

            if (state == DRAW_STATE)
            {
                textOffsetX = 0;
                i = startLine;
                glyphWidth = 0;

                // Save character position when we switch states
                int tmp = lastk;
                lastk = k - 1;
                k = tmp;
            }
        }
        else
        {
            if (codepoint == '\n')
            {
                if (!wordWrap)
                {
                    textOffsetY += (font.baseSize + font.baseSize/2)*scaleFactor;
                    textOffsetX = 0;
                }
            }
            else
            {
                if (!wordWrap && ((textOffsetX + glyphWidth) > rec.width))
                {
                    textOffsetY += (font.baseSize + font.baseSize/2)*scaleFactor;
                    textOffsetX = 0;
                }

                // When text overflows rectangle height limit, just stop drawing
                if ((textOffsetY + font.baseSize*scaleFactor) > rec.height) break;

                // Draw selection background
                bool isGlyphSelected = false;
                if ((selectStart >= 0) && (k >= selectStart) && (k < (selectStart + selectLength)))
                {
                    DrawRectangleRec((Rectangle){ rec.x + textOffsetX - 1, rec.y + textOffsetY, glyphWidth, (float)font.baseSize*scaleFactor }, selectBackTint);
                    isGlyphSelected = true;
                }

                // Draw current character glyph
                if ((codepoint != ' ') && (codepoint != '\t'))
                {
                    DrawTextCodepoint(font, codepoint, (Vector2){ rec.x + textOffsetX, rec.y + textOffsetY }, fontSize, isGlyphSelected? selectTint : tint);
                }
            }

            if (wordWrap && (i == endLine))
            {
                textOffsetY += (font.baseSize + font.baseSize/2)*scaleFactor;
                textOffsetX = 0;
                startLine = endLine;
                endLine = -1;
                glyphWidth = 0;
                selectStart += lastk - k;
                k = lastk;

                state = !state;
            }
        }

        textOffsetX += glyphWidth;
    }
}

static void DrawTextRec(Font font, const char *text, Rectangle rec, float fontSize, float spacing, bool wordWrap, Color tint)
{
    DrawTextBoxedSelectable(font, text, rec, fontSize, spacing, wordWrap, tint, 0, 0, WHITE, WHITE);
}

// _____________________________________________________________________________
//
//  Structs & Variables
// _____________________________________________________________________________
//

typedef struct Window
{
    int x, y;
    int width, height;
    int minWidth, minHeight; // minimum size of the window (if resizable)
    bool active;             // if true, this window slot is taken and the window is shown on screen
    bool minimized, maximized;
    bool resizable;
    Rectangle oldPos;   // old window coords are saved here when the window is maximized
    void (*function)(); // pointer to the function that is executed on this window every frame
    void *data;         // storage for window related variables
    const char *title;

    // for message boxes
    const char *message;
    int icon; // index of the icons array
} Window;

typedef enum
{
    IC_ERROR,
    IC_LOGO,
    IC_NOSLOTS,
    IC_ENDSESSION,
    IC_COUNT
} Icon;

Font font = {0};
Font boldFont = {0};
Texture icons[IC_COUNT];
Texture winButtons[8]; // close, maximize, minimize, etc
Texture smallButtons[2];
Texture largeButtons[2];
Texture startButtons[2];

Window windows[WINDOW_LIMIT + 1]; // last window slot is reserved, second to last window is focused
bool moving = false;              // is the focused window being moved?
bool resizing = false;            // is the focused window being resized?
Vector2 hook = {0};               // mouse position relative to the focused window when it is started to be moved

int cursor = MOUSE_CURSOR_DEFAULT; // mouse cursor style, updated every frame
char timebuf[16];                  // string to store current time
bool running = true;               // if set to false, clean up and exit

// _____________________________________________________________________________
//
//  Utility functions
// _____________________________________________________________________________
//

// Gives focus to the specified window.
void focusWindow(int index)
{
    Window temp = windows[index];
    for (int i = index; i < WINDOW_LIMIT; i++)
        windows[i] = windows[i + 1];

    windows[WINDOW_LIMIT - 1] = temp;
}

// Draws text inside a window.
void winDrawText(Window *window, const char *text, int x, int y)
{
    DrawTextRec(
        font, text,
        (Rectangle){
            window->x + 2 + x,
            window->y + 16 + y,
            window->width - 2 - x,
            window->height - 16 - y},
        FONT_SIZE, 0.0f, true,
        WINDOW_TEXT_COLOR);

#ifdef DEBUG_WINDRAWTEXT
    DrawRectangleLines(
        window->x + 2 + x,
        window->y + 16 + y,
        window->width - 2 - x,
        window->height - 16 - y,
        BLACK);
#endif
}

// Draws a texture inside a window.
void winDrawTexture(Window *window, Texture *texture, int x, int y)
{
    DrawTexture(*texture, window->x + 2 + x, window->y + 16 + y, WHITE);
}

// Draws a button inside a window, returns true if the button was clicked.
bool winButton(Window *window, int index, const char *text, int x, int y, bool large)
{
    bool hovered = focused(index) && mcollide((window->x + 2 + x), (window->y + 16 + y), (48 * (large + 1)), 16);
    Texture texture = (large ? largeButtons : smallButtons)[hovered && lmbdown];

    winDrawTexture(window, &texture, x, y);
    winDrawText(window, text, x + 2, y + 2);

    return hovered && lmbup;
}

void messageBoxWindow(Window *window, int index);

// Opens a new window.
bool createWindow(Window window)
{
    window.active = true;
    window.minimized = false;
    window.maximized = false;

    // find a free window slot
    int slot = -1;
    for (int i = 0; i < WINDOW_LIMIT; i++)
        if (!windows[i].active) slot = i;

    if (slot != -1)
    {
        // if a slot was found, assign it to the window and focus it
        windows[slot] = window;
        focusWindow(slot);
        return true;
    }
    else
    {
        // if all window slots are taken, show an error message
        windows[WINDOW_LIMIT] = (Window){
            .x = RENDER_WIDTH / 2 - 100,
            .y = RENDER_HEIGHT / 2 - 50,
            .width = 200,
            .height = 100,
            .active = true,
            .function = messageBoxWindow,
            .title = "Error",
            .message = "Out of window slots! Close some windows and try again.",
            .icon = IC_NOSLOTS};
        return false;
    }
}

// _____________________________________________________________________________
//
//  Window controller functions
// _____________________________________________________________________________
//

void messageBoxWindow(Window *window, int index)
{
    winDrawTexture(window, &icons[window->icon], 8, 8);
    winDrawText(window, window->message, 48, 8);

    if (winButton(window, index, "OK", 48, 64, false))
        window->active = false;
}

void endSessionWindow(Window *window, int index)
{
    winDrawTexture(window, &icons[IC_ENDSESSION], 8, 8);
    winDrawText(window, "Are you sure you want to end your session?", 48, 8);

    if (winButton(window, index, "Yes", 48, 64, false))
        running = false;
    if (winButton(window, index, "No", 100, 64, false))
        window->active = false;
}

// Window used for demonstrating window-bound variable storage.
void testWindow(Window *window, int index)
{
    winDrawText(window, TextFormat("%d", window->data), 0, 0);

    if (winButton(window, index, "Increase", 0, 20, 1))
        window->data++;
    if (winButton(window, index, "Decrease", 0, 36, 1))
        window->data--;
}

void startMenuWindow(Window *window, int index)
{
    // if this window loses focus, close it
    if (!focused(index)) window->active = false;

    // check each window, if another start menu is open, don't create a new one
    for (int i = 0; i < WINDOW_LIMIT + 1; i++)
    {
        if (windows[i].active && windows[i].function == startMenuWindow && i != index)
        {
            window->active = false;
        }
    }

    // force the start menu to stay in one location
    window->x = 0;
    window->y = RENDER_HEIGHT / 2;

    if (winButton(window, index, "Exit", 0, 0, true))
    {
        window->active = false;

        createWindow((Window){
            .x = RENDER_WIDTH / 2 - 100,
            .y = RENDER_HEIGHT / 2 - 50,
            .width = 200,
            .height = 100,
            .title = "End session",
            .function = endSessionWindow});
    }

    if (winButton(window, index, "window.data test", 0, 16, true))
    {
        window->active = false;

        createWindow((Window){
            .x = RENDER_WIDTH / 2 - 100,
            .y = RENDER_HEIGHT / 2 - 50,
            .width = 200,
            .height = 100,
            .title = "window.data test",
            .function = testWindow});
    }
}

// _____________________________________________________________________________
//
//  Main
// _____________________________________________________________________________
//

int main()
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "rlwm");
    SetTargetFPS(60);
    SetWindowIcon(LoadImage(TextFormat("%s/logo.png", ASSETS_FOLDER)));
    SetMouseScale(1 / SCALE, 1 / SCALE);

    RenderTexture rt = LoadRenderTexture(RENDER_WIDTH, RENDER_HEIGHT);

    if (FULLSCREEN) ToggleFullscreen();

    font = LoadFontEx(TextFormat("%s/font.ttf", ASSETS_FOLDER), FONT_SIZE, NULL, 95);
    boldFont = LoadFontEx(TextFormat("%s/font_bold.ttf", ASSETS_FOLDER), FONT_SIZE, NULL, 95);

    Texture bg = LoadTexture(TextFormat("%s/bg.png", ASSETS_FOLDER));

    // _________________________________________________________________________
    //
    //  Load button images
    // _________________________________________________________________________
    //

    Image buttonImage = LoadImage(TextFormat("%s/buttons.png", ASSETS_FOLDER));

    // window control buttons
    for (int i = 0; i < 8; i++)
    {
        Image button = ImageCopy(buttonImage);
        ImageCrop(&button, (Rectangle){i * 12, 0, 12, 12});
        winButtons[i] = LoadTextureFromImage(button);
        UnloadImage(button);
    }

    for (int i = 0; i < 2; i++)
    {
        // small buttons (48 × 16)
        Image sbutton = ImageCopy(buttonImage);
        ImageCrop(&sbutton, (Rectangle){i * 48, 12, 48, 16});
        smallButtons[i] = LoadTextureFromImage(sbutton);
        UnloadImage(sbutton);

        // large buttons (96 × 16)
        Image lbutton = ImageCopy(buttonImage);
        ImageCrop(&lbutton, (Rectangle){0, 28 + i * 16, 96, 16});
        largeButtons[i] = LoadTextureFromImage(lbutton);
        UnloadImage(lbutton);

        // start buttons (48 × 16)
        Image stbutton = ImageCopy(buttonImage);
        ImageCrop(&stbutton, (Rectangle){i * 48, 60, 48, 16});
        startButtons[i] = LoadTextureFromImage(stbutton);
        UnloadImage(stbutton);
    }

    UnloadImage(buttonImage);

    // _________________________________________________________________________
    //
    //  Load icons
    // _________________________________________________________________________
    //

    Image iconImage = LoadImage(TextFormat("%s/icons.png", ASSETS_FOLDER));

    for (int i = 0; i < IC_COUNT; i++)
    {
        Image icon = ImageCopy(iconImage);
        ImageCrop(&icon, (Rectangle){i * 32, 0, 32, 32});
        icons[i] = LoadTextureFromImage(icon);
        UnloadImage(icon);
    }

    UnloadImage(iconImage);

    // _________________________________________________________________________
    //
    //  Main Loop
    // _________________________________________________________________________
    //

    createWindow((Window){
        .x = 50,
        .y = 80,
        .width = 224,
        .height = 100,
        .minWidth = 224,
        .minHeight = 100,
        .resizable = true,
        .function = messageBoxWindow,
        .title = "Testing",
        .message = "Example message box window\nPress A to create new windows",
        .icon = IC_LOGO});

    while (running && !WindowShouldClose())
    {
        // _____________________________________________________________________
        //
        //  Update
        // _____________________________________________________________________
        //

        if (IsKeyPressed(KEY_A))
        {
            createWindow((Window){
                .x = GetRandomValue(0, RENDER_WIDTH - 200),
                .y = GetRandomValue(0, RENDER_HEIGHT - 100),
                .width = 200,
                .height = 100,
                .minWidth = 125,
                .minHeight = 100,
                .resizable = true,
                .function = messageBoxWindow,
                .title = "New window",
                .message = "hello world",
                .icon = IC_ERROR});
        }

        SetMouseCursor(cursor);
        cursor = MOUSE_CURSOR_DEFAULT;

        // _____________________________________________________________________
        //
        //  Window focusing
        // _____________________________________________________________________
        //

        for (int i = WINDOW_LIMIT - 1; i > -1; i--)
        {
            Window win = windows[i];
            if (!win.active || win.minimized) continue;

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && mcollide(win.x, win.y, win.width, win.height))
            {
                focusWindow(i);
                moving = false;
                resizing = false;
                break;
            }
        }

        // _____________________________________________________________________
        //
        //  Window movement
        // _____________________________________________________________________
        //

        Window *win = &windows[WINDOW_LIMIT - 1];

        if (lmbup)
        {
            moving = false;
            resizing = false;
        }

        // if titlebar is clicked on, start moving the window
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && mcollide(win->x, win->y, win->width - 40, 16))
        {
            moving = true;
            resizing = false;
            hook.x = GetMouseX() - win->x;
            hook.y = GetMouseY() - win->y;
        }

        // if window is being moved, update its location
        if (moving)
        {
            // if the window was maximized, restore it
            if (win->maximized)
            {
                win->maximized = false;

                win->width = win->oldPos.width;
                win->height = win->oldPos.height;
                win->x = GetMouseX() - win->width / 2;
                win->y = 0;

                hook.x = GetMouseX() - win->x;
                hook.y = GetMouseY() - win->y;
            }
            cursor = MOUSE_CURSOR_RESIZE_ALL;
            win->x = GetMouseX() - hook.x;
            win->y = GetMouseY() - hook.y;
        }

        // _____________________________________________________________________
        //
        //  Window resizing
        // _____________________________________________________________________
        //

        // if bottom right corner is hovered over, change the cursor
        if (mcollide(win->x + win->width - 4, win->y + win->height - 4, 8, 8) && win->resizable)
        {
            cursor = MOUSE_CURSOR_RESIZE_NWSE;
            // if bottom right corner is clicked, start resizing
            if (lmbdown)
            {
                moving = false;
                resizing = true;
            }
        }

        if (resizing)
        {
            win->width = GetMouseX() - win->x;
            win->height = GetMouseY() - win->y;

            // make sure the window is not below its minimum size
            if (win->width < win->minWidth)
                win->width = win->minWidth;
            if (win->height < win->minHeight)
                win->height = win->minHeight;
        }

        // _____________________________________________________________________
        //
        //  Draw wallpaper
        // _____________________________________________________________________
        //

        BeginTextureMode(rt);
        ClearBackground(BLACK);

        // draw tiled/scaled background
        if (TILED_BACKGROUND)
        {
            DrawTextureTiled(
                bg, (Rectangle){0, 0, bg.width, bg.height},
                (Rectangle){0, 0, RENDER_WIDTH, RENDER_HEIGHT},
                (Vector2){0, 0}, 0.0f, 1.0f, WHITE);
        }
        else
        {
            DrawTexturePro(
                bg, (Rectangle){0, 0, bg.width, bg.height},
                (Rectangle){0, 0, RENDER_WIDTH, RENDER_HEIGHT},
                (Vector2){0, 0}, 0.0f, WHITE);
        }

        // _____________________________________________________________________
        //
        //  Draw windows
        // _____________________________________________________________________
        //

        for (int i = 0; i < WINDOW_LIMIT + 1; i++)
        {
            Window *win = &windows[i];
            if (!win->active || win->minimized) continue;

            // draw window shadow
            DrawRectangle(
                win->x + SHADOW_OFFSET.x, win->y + SHADOW_OFFSET.y,
                win->width, win->height, SHADOW_COLOR);

            // draw window background and titlebar
            DrawRectangle(win->x, win->y, win->width, win->height, WINDOW_BG_COLOR);
            DrawRectangle(
                win->x + 1, win->y + 1, win->width - 2, 14,
                focused(i) ? TITLE_BG_COLOR : TITLE_UNFOCUSED_COLOR);

            // draw title text
            const char *title = win->title;
            if (resizing && focused(i))
                title = TextFormat("%d x %d", win->width, win->height);
            else if (moving && focused(i))
                title = TextFormat("%d, %d", win->x, win->y);
            DrawTextEx(boldFont, title, (Vector2){win->x + 2, win->y + 2}, FONT_SIZE, 0.0f, TITLE_TEXT_COLOR);

            // _________________________________________________________________
            //
            //  Draw window buttons
            // _________________________________________________________________
            //

            // close button
            bool hoverclose = mcollide(win->x + win->width - 14, win->y + 2, 12, 12);
            DrawTexture(
                winButtons[3 + (lmbdown && hoverclose) * 4],
                win->x + win->width - 14, win->y + 2, WHITE);
            if (hoverclose && lmbup) win->active = false;

            // maximize/restore button
            bool hovermax = mcollide(win->x + win->width - 27, win->y + 2, 12, 12);
            DrawTexture(
                winButtons[1 + win->maximized + (lmbdown && hovermax) * 4],
                win->x + win->width - 27, win->y + 2, WHITE);
            if (hovermax && lmbup)
            {
                win->maximized = !win->maximized;
                if (win->maximized)
                {
                    // if window is maximized, save its old coords in oldPos
                    win->oldPos = (Rectangle){win->x, win->y, win->width, win->height};
                    win->x = 0;
                    win->y = 0;
                    win->width = RENDER_WIDTH;
                    win->height = RENDER_HEIGHT - 18;
                }
                else
                {
                    // if window is restored, retrieve its coords from oldPos
                    win->x = win->oldPos.x;
                    win->y = win->oldPos.y;
                    win->width = win->oldPos.width;
                    win->height = win->oldPos.height;
                }
            }

            // minimize button
            bool hovermin = mcollide(win->x + win->width - 40, win->y + 2, 12, 12);
            DrawTexture(
                winButtons[0 + (lmbdown && hovermin) * 4],
                win->x + win->width - 40, win->y + 2, WHITE);
            if (hovermin && lmbup) win->minimized = true;

            // force window to be at least partially on screen
            if (win->x > RENDER_WIDTH - 5)
                win->x = RENDER_WIDTH - 5;
            if (win->y > RENDER_HEIGHT - 20)
                win->y = RENDER_HEIGHT - 20;
            if (win->width < 50)
                win->width = 50;
            if (win->height < 25)
                win->width = 24;

            win->function(win, i);
        }

#ifdef DEBUG_MOVERESIZE
        DrawText(TextFormat("Moving: %d  Resizing: %d", moving, resizing), 0, 0, 10, WHITE);
#endif

        // _____________________________________________________________________
        //
        //  Draw taskbar
        // _____________________________________________________________________
        //

        bool starthover = mcollide(1, RENDER_HEIGHT - 17, 48, 16);
        DrawRectangle(0, RENDER_HEIGHT - 18, RENDER_WIDTH, 18, TASKBAR_BG_COLOR);
        DrawTexture(startButtons[starthover && lmbdown], 1, RENDER_HEIGHT - 17, WHITE);

        if (starthover && lmbup)
        {
            createWindow((Window){
                .x = 0,
                .y = RENDER_HEIGHT / 2,
                .width = 100,
                .height = RENDER_HEIGHT / 2 - 18,
                .title = "Start menu",
                .function = startMenuWindow});
        }

        // draw buttons for minimized windows
        int x = 50;
        for (int i = 0; i < WINDOW_LIMIT; i++)
        {
            if (!windows[i].minimized) continue;

            bool winbtnhover = mcollide(x, RENDER_HEIGHT - 17, 96, 16);
            DrawTexture(largeButtons[winbtnhover && lmbdown], x, RENDER_HEIGHT - 17, WHITE);
            DrawTextEx(
                font, windows[i].title, (Vector2){x + 1, RENDER_HEIGHT - 16},
                FONT_SIZE, 0.0f, TASKBAR_TEXT_COLOR);

            if (winbtnhover && lmbup)
            {
                windows[i].minimized = false;
                focusWindow(i);
            }

            x += 97;
        }

        // draw current time on the taskbar
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);

        strftime(timebuf, 16, "%H:%M:%S", tm);
        DrawTextEx(
            font, timebuf,
            (Vector2){RENDER_WIDTH - MeasureTextEx(font, timebuf, FONT_SIZE, 0.0f).x - 3, RENDER_HEIGHT - 15},
            FONT_SIZE, 0.0f, TASKBAR_TEXT_COLOR);

        // _____________________________________________________________________
        //
        //  Render to screen
        // _____________________________________________________________________
        //

        EndTextureMode();
        BeginDrawing();

        // render textures have to be vertically flipped when drawing them
        DrawTexturePro(
            rt.texture,
            (Rectangle){0, 0, RENDER_WIDTH, -RENDER_HEIGHT},
            (Rectangle){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT},
            (Vector2){0, 0}, 0.0f, WHITE);

        EndDrawing();
    }

    // _________________________________________________________________________
    //
    //  Unload and exit
    // _________________________________________________________________________
    //

    UnloadFont(font);
    UnloadFont(boldFont);
    UnloadTexture(bg);
    UnloadRenderTexture(rt);

    for (int i = 0; i < IC_COUNT; i++) UnloadTexture(icons[i]);
    for (int i = 0; i < 8; i++) UnloadTexture(winButtons[i]);
    
    // small,large,start
    for (int i = 0; i < 2; i++)
    {
        UnloadTexture(smallButtons[i]);
        UnloadTexture(largeButtons[i]);
        UnloadTexture(startButtons[i]);
    }

    CloseWindow();
    return 0;
}