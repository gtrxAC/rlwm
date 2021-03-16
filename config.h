#define SCREEN_WIDTH		1024
#define SCREEN_HEIGHT		768
#define FULLSCREEN			0
#define WINDOW_LIMIT		8
#define SCALE				2.0f
#define FONT_SIZE			13.0f
#define TILED_BACKGROUND	1

// #define DEBUG_WINDRAWTEXT
// #define DEBUG_MOVERESIZE

#define DEFAULT_THEME
// #define YOUR_THEME

#ifdef DEFAULT_THEME
	#define ASSETS_FOLDER				"default"
	#define WINDOW_BG_COLOR				(Color)  {192, 176, 176, 255}
	#define WINDOW_TEXT_COLOR			(Color)  {0,   0,   0,   255}
	#define TITLE_BG_COLOR				(Color)  {64,  80,  144, 255}
	#define TITLE_UNFOCUSED_COLOR		(Color)  {80,  80,  96,  255}
	#define TITLE_TEXT_COLOR			(Color)  {255, 255, 255, 255}
	#define TASKBAR_BG_COLOR			(Color)  {192, 176, 176, 255}
	#define TASKBAR_TEXT_COLOR			(Color)  {0,   0,   0,   255}
	#define SHADOW_COLOR				(Color)  {0,   0,   0,   128}
	#define SHADOW_OFFSET				(Vector2){2,   2}

// create your own themes here...
// #elif defined YOUR_THEME
//     #define ...
#endif