#ifndef INCLUDED_UI_MENU_H
#define INCLUDED_UI_MENU_H

/*** Constants ***/

/* Colors for interactive menus */
enum
{
	CURS_UNKNOWN = 0,		/* Use gray / dark blue for cursor */
	CURS_KNOWN = 1			/* Use white / light blue for cursor */
};

/* Cursor colours for different states */
extern const byte curs_attrs[2][2];

/* Standard menu orderings */
extern const char lower_case[];			/* abc..z */
extern const char upper_case[];			/* ABC..Z */


/*
  Together, these classes define the constant properties of
  the various menu classes.

  A menu consists of:
   - menu_iter, which describes how to handle the type of "list" that's 
     being displayed as a menu
   - a menu_skin, which describes the layout of the menu on the screen.
   - various bits and bobs of other data (e.g. the actual list of entries)
 */
typedef struct menu_type menu_type;



/*** Predefined menu "skins" ***/

/**
 * Types of predefined skins available.
 */
typedef enum
{
	/*
	 * A simple list of actions with an associated name and id.
	 * Private data: an array of menu_action
	 */
	MN_ITER_ACTIONS = 1,

	/*
	 * Slightly more sophisticated, a list of menu items that also
	 * allows per-item flags and a "selection" character to be specified.
	 * Private data: an array of menu_item
	 */
	MN_ITER_ITEMS   = 2,

	/*
	 * A list of strings to be selected from - no associated actions.
	 * Private data: an array of const char *
	 */
	MN_ITER_STRINGS = 3
} menu_iter_id;


/**
 * Primitive menu item with bound action.
 */
typedef struct
{
	/* Tag (optional) */
	char tag;

	/* Name of the action */
	const char *name;

	/* Action to perform */
	void (*action)(const char *title, int row);
} menu_action;


/**
 * Decorated menu item with bound action
 *
 * XXX this menu type is used in one place, cmd4.c, just to allow flags.
 * XXX note that in practice, menu_action.id can be used as a selection key
 * XXX it's totally worth merging the two
 */
typedef struct
{
	menu_action act; /* Base type */
	char sel;        /* Character used for selection, if special-purpose */
	                 /* bindings are desired. */
	int flags;	 /* State of the menu item.  See menu flags below */
} menu_item;


/**
 * Flags for menu_items.
 *
 * XXX DISABLED, SELECTABLE, and HIDDEN are all unused and may not work
 * XXX MN_GRAYED and GREYED?  wtf?
 */
#define MN_DISABLED   0x0100000 /* Neither action nor selection is permitted */
#define MN_GRAYED     0x0200000 /* Row is displayed with CURS_UNKNOWN colors */
#define MN_GREYED     0x0200000 /* Row is displayed with CURS_UNKNOWN colors */
#define MN_SELECTED   0x0400000 /* Row is currently selected */
#define MN_SELECTABLE 0x0800000 /* Row is permitted to be selected */
#define MN_HIDDEN     0x1000000 /* Row is hidden, but may be selected via */
                                /* key-binding.  */


/**
 * Underlying function set for displaying lists in a certain kind of way.
 */
typedef struct
{
	/* Returns menu item tag (optional) */
	char (*get_tag)(menu_type *menu, int oid);

	/*
	 * Validity checker (optional--all rows are assumed valid if not present)
 	 * Return values will be interpreted as: 0 = no, 1 = yes, 2 = hide.
 	 */
	int (*valid_row)(menu_type *menu, int oid);

	/* Displays a menu row */
	void (*display_row)(menu_type *menu, int pos, bool cursor,
			int row, int col, int width);

	/* Handle 'positive' events (selections or cmd_keys) */
	/* XXX split out into a select handler and a cmd_key handler */
	bool (*row_handler)(menu_type *menu, const ui_event_data *event, int oid);

	/* Called when the screen resizes */
	void (*resize)(menu_type *m);
} menu_iter;



/*** Menu skins ***/

/**
 * Identifiers for the kind of layout to use
 */
typedef enum
{
	MN_SKIN_SCROLL = 1,   /**< Ordinary scrollable single-column list */
	MN_SKIN_COLUMNS = 2   /**< Multicolumn view */
} skin_id;


/* Class functions for menu layout */
typedef struct
{
	/* Determines the cursor index given a (mouse) location */
	int (*get_cursor)(int row, int col, int n, int top, region *loc);

	/* Displays the current list of visible menu items */
	void (*display_list)(menu_type *menu, int cursor, int *top, region *);

	/* Specifies the relative menu item given the state of the menu */
	char (*get_tag)(menu_type *menu, int pos);
} menu_skin;



/*** Base menu structure ***/

/**
 * Flags for menu appearance & behaviour
 */
enum
{
	/* Tags are associated with the view, not the element */
	MN_REL_TAGS = 0x0100,

	/* No tags -- movement key and mouse browsing only */
	MN_NO_TAGS = 0x0200,

	/* Tags to be generated by the display function */
	MN_PVT_TAGS = 0x0400,

	/* Tag selections can be made regardless of the case of the key pressed. 
	 * i.e. 'a' activates the line tagged 'A'. */
	MN_CASELESS_TAGS = 0x0800,

	/* double tap (or keypress) for selection; single tap is cursor movement */
	MN_DBL_TAP = 0x1000 
} menu_type_flags;


/* Base menu type */
struct menu_type
{
	/** Public variables **/
	const char *title;
	const char *prompt;

	/* Keyboard shortcuts for menu selection-- shouldn't overlap cmd_keys */
	const char *selections; 

	/* String of characters that when pressed, menu handler should be called */
	/* Mustn't overlap with 'selections' or some items may be unselectable */
	const char *cmd_keys;

  	/* auxiliary browser help function */
	void (*browse_hook)(int oid, void *db, const region *loc);

	/* Flags specifying the behavior of this menu (from menu_type_flags) */
	int flags;


	/** Private variables **/

	/* Stored boundary, set by menu_layout().  This is used to calculate
	 * where the menu should be displayed on display & resize */
	region boundary;

	int filter_count;        /* number of rows in current view */
	const int *filter_list;  /* optional filter (view) of menu objects */

	int count;               /* number of rows in underlying data set */
	void *menu_data;         /* the data used to access rows. */

	const menu_skin *skin;      /* menu display style functions */
	const menu_iter *row_funcs; /* menu skin functions */

	/* State variables */
	int cursor;             /* Currently selected row */
	int top;                /* Position in list for partial display */
	region active;          /* Subregion actually active for selection */

};



/*** Menu API ***/

/**
 * Initialise a menu, using the skin and iter functions specified.
 */
void menu_init(menu_type *menu, skin_id skin, const menu_iter *iter);


/**
 * Given a predefined menu kind, return its iter functions.
 */
const menu_iter *menu_find_iter(menu_iter_id iter_id);


/**
 * Set menu private data and the number of menu items.
 *
 * Menu private data is then available from inside menu callbacks using
 * menu_priv().
 */
void menu_setpriv(menu_type *menu, int count, void *data);


/**
 * Return menu private data, set with menu_setpriv().
 */
void *menu_priv(menu_type *menu);


/*
 * Set a filter on what items a menu can display.
 *
 * Use this if your menu private data has 100 items, but you want to choose
 * which ones of those to display at any given time, e.g. in an inventory menu.
 * object_list[] should be an array of indexes to display, and n should be its
 * length.
 */
void menu_set_filter(menu_type *menu, const int object_list[], int n);


/**
 * Remove any filters set on a menu by menu_set_filer().
 */
void menu_release_filter(menu_type *menu);


/**
 * Ready a menu for display in the region specified.
 *
 * XXX not ready for dynamic resizing just yet
 */
bool menu_layout(menu_type *menu, const region *loc);


/**
 * Display a menu.
 */
void menu_refresh(menu_type *menu);


/**
 * Run a menu.
 *
 * 'notify' is a bitwise OR of ui_event_type events that you want to
 * menu_select to return to you if they're not handled inside the menu loop.
 * e.g. if you want to handle key events without specifying a menu_iter->handle
 * function, you can set notify to EVT_KBRD, and any non-navigation keyboard
 * events will stop the menu loop and return them to you.
 *
 * Some events are returned by default, and else are EVT_ESCAPE and EVT_SELECT.
 * 
 * Event types that can be returned:
 *   EVT_ESCAPE: no selection; go back (by default)
 *   EVT_SELECT: menu->cursor is the selected menu item (by default)
 *   EVT_MOVE:   the cursor has moved
 *   EVT_KBRD:   unhandled keyboard events
 *   EVT_MOUSE:  unhandled mouse events  
 *   EVT_RESIZE: resize events
 * 
 * XXX remove 'notify'
 */
ui_event_data menu_select(menu_type *menu, int notify);



/* Interal menu stuff that cmd-know needs because it's quite horrible */
bool menu_handle_mouse(menu_type *menu, const ui_event_data *in, ui_event_data *out);
bool menu_handle_keypress(menu_type *menu, const ui_event_data *in, ui_event_data *out);

#endif /* INCLUDED_UI_MENU_H */
