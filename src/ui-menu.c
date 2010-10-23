/*
 * File: ui-menu.c
 * Purpose: Generic menu interaction functions
 *
 * Copyright (c) 2007 Pete Mack
 * Copyright (c) 2010 Andi Sidwell
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */
#include "angband.h"
#include "ui-event.h"
#include "ui-menu.h"

/* Cursor colours */
const byte curs_attrs[2][2] =
{
	{ TERM_SLATE, TERM_BLUE },      /* Greyed row */
	{ TERM_WHITE, TERM_L_BLUE }     /* Valid row */
};

/* Some useful constants */
const char lower_case[] = "abcdefghijklmnopqrstuvwxyz";
const char upper_case[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

/* forward declarations */
static void display_menu_row(menu_type *menu, int pos, int top,
			     bool cursor, int row, int col, int width);
static bool menu_calc_size(menu_type *menu);

/* Display an event, with possible preference overrides */
static void display_action_aux(menu_action *act, byte color, int row, int col, int wid)
{
	/* TODO: add preference support */
	/* TODO: wizard mode should show more data */
	Term_erase(col, row, wid);

	if (act->name)
		Term_putstr(col, row, wid, color, act->name);
}

/* ------------------------------------------------------------------------
 * MN_ACTIONS HELPER FUNCTIONS
 *
 * MN_ACTIONS is the type of menu iterator that displays a simple list of
 * menu_actions.
 * ------------------------------------------------------------------------ */

static char menu_action_tag(menu_type *m, int oid)
{
	menu_action *acts = menu_priv(m);

	if (acts[oid].tag)
		return acts[oid].tag;

	return 0;
}

static int menu_action_valid(menu_type *m, int oid)
{
	menu_action *acts = menu_priv(m);
	return acts[oid].name ? TRUE : FALSE;
}

static void menu_action_display(menu_type *m, int oid, bool cursor, int row, int col, int width)
{
	menu_action *acts = menu_priv(m);
	byte color = curs_attrs[CURS_KNOWN][0 != cursor];

	display_action_aux(&acts[oid], color, row, col, width);
}

static bool menu_action_handle(menu_type *m, const ui_event_data *event, int oid)
{
	menu_action *acts = menu_priv(m);

	if (event->type == EVT_SELECT && acts[oid].action)
	{
		acts[oid].action(acts[oid].name, m->cursor);
		return TRUE;
	}

	return FALSE;
}


/* Virtual function table for action_events */
const menu_iter menu_iter_actions =
{
	menu_action_tag,
	menu_action_valid,
	menu_action_display,
	menu_action_handle,
	NULL
};


/* ------------------------------------------------------------------------
 * MN_ITEMS HELPER FUNCTIONS
 *
 * MN_ITEMS is the type of menu iterator that displays a simple list of 
 * menu_items (i.e. menu_actions with optional per-item flags and 
 * "selection" keys.
 * ------------------------------------------------------------------------ */

static char item_menu_tag(menu_type *m, int oid)
{
	menu_item *items = menu_priv(m);
	return items[oid].sel;
}

static int item_menu_valid(menu_type *m, int oid)
{
	menu_item *items = menu_priv(m);

	if (items[oid].flags & MN_HIDDEN)
		return 2;

	return (NULL != items[oid].act.name);
}

static void item_menu_display(menu_type *m, int oid, bool cursor, int row, int col, int width)
{
	menu_item *items = menu_priv(m);
	byte color = curs_attrs[!(items[oid].flags & (MN_GRAYED))][0 != cursor];

	display_action_aux(&items[oid].act, color, row, col, width);
}

static bool item_menu_handle(menu_type *m, const ui_event_data *event, int oid)
{
	menu_item *items = menu_priv(m);

	if (event->type == EVT_SELECT)
	{
		menu_item *item = &items[oid];

		if (item->flags & MN_DISABLED)
			return TRUE;

		if (item->act.action)
			item->act.action(item->act.name, m->cursor);

		if (item->flags & MN_SELECTABLE)
			item->flags ^= MN_SELECTED;

		return TRUE;
	}

	return FALSE;
}

/* Virtual function table for menu items */
const menu_iter menu_iter_items =
{
	item_menu_tag,       /* get_tag() */
	item_menu_valid,     /* valid_row() */
	item_menu_display,   /* display_row() */
	item_menu_handle,     /* row_handler() */
	NULL
};

/* ------------------------------------------------------------------------
 * MN_STRINGS HELPER FUNCTIONS
 *
 * MN_STRINGS is the type of menu iterator that displays a simple list of 
 * strings - no action is associated, as selection will just return the index.
 * ------------------------------------------------------------------------ */
static void display_string(menu_type *m, int oid, bool cursor,
		int row, int col, int width)
{
	const char **items = menu_priv(m);
	byte color = curs_attrs[CURS_KNOWN][0 != cursor];
	Term_putstr(col, row, width, color, items[oid]);
}

/* Virtual function table for displaying arrays of strings */
const menu_iter menu_iter_strings =
{ 
	NULL,              /* get_tag() */
	NULL,              /* valid_row() */
	display_string,    /* display_row() */
	NULL, 	           /* row_handler() */
	NULL
};




/* ================== SKINS ============== */


/* Scrolling menu */
/* Find the position of a cursor given a screen address */
static int scrolling_get_cursor(int row, int col, int n, int top, region *loc)
{
	int cursor = row - loc->row + top;
	if (cursor >= n) cursor = n - 1;

	return cursor;
}


/* Display current view of a skin */
static void display_scrolling(menu_type *menu, int cursor, int *top, region *loc)
{
	int col = loc->col;
	int row = loc->row;
	int rows_per_page = loc->page_rows;
	int n = menu->filter_count;
	int i;

	/* Keep a certain distance from the top when possible */
	if ((cursor <= *top) && (*top > 0))
		*top = cursor - 1;

	/* Keep a certain distance from the bottom when possible */
	if (cursor >= *top + (rows_per_page - 1))
		*top = cursor - (rows_per_page - 1) + 1;

	/* Limit the top to legal places */
	*top = MIN(*top, n - rows_per_page);
	*top = MAX(*top, 0);

	for (i = 0; i < rows_per_page; i++)
	{
		/* Blank all lines */
		Term_erase(col, row + i, loc->width - col);
		if (i < n)
		{
			/* Redraw the line if it's within the number of menu items */
			bool is_curs = (i == cursor - *top);
			display_menu_row(menu, i + *top, *top, is_curs, row + i, col,
							loc->width);
		}
	}

	if (menu->cursor >= 0)
		Term_gotoxy(col, row + cursor - *top);
}

static char scroll_get_tag(menu_type *menu, int pos)
{
	if (menu->selections)
		return menu->selections[pos - menu->top];

	return 0;
}

/* Virtual function table for scrollable menu skin */
const menu_skin menu_skin_scroll =
{
	scrolling_get_cursor,
	display_scrolling,
	scroll_get_tag
};


/* Multi-column menu */
/* Find the position of a cursor given a screen address */
static int columns_get_cursor(int row, int col, int n, int top, region *loc)
{
	int rows_per_page = loc->page_rows;
	int colw = loc->width / (n + rows_per_page - 1) / rows_per_page;
	int cursor = row + rows_per_page * (col - loc->col) / colw;

	if (cursor < 0) cursor = 0;	/* assert: This should never happen */
	if (cursor >= n) cursor = n - 1;

	return cursor;
}

static void display_columns(menu_type *menu, int cursor, int *top, region *loc)
{
	int c, r;
	int w, h;
	int n = menu->filter_count;
	int col = loc->col;
	int row = loc->row;
	int rows_per_page = loc->page_rows;
	int cols = (n + rows_per_page - 1) / rows_per_page;
	int colw = 23;

	Term_get_size(&w, &h);

	if ((colw * cols) > (w - col))
		colw = (w - col) / cols;

	for (c = 0; c < cols; c++)
	{
		for (r = 0; r < rows_per_page; r++)
		{
			int pos = c * rows_per_page + r;
			bool is_cursor = (pos == cursor);
			display_menu_row(menu, pos, 0, is_cursor, row + r, col + c * colw,
							 colw);
		}
	}
}

static char column_get_tag(menu_type *menu, int pos)
{
	if (menu->selections)
		return menu->selections[pos];

	return 0;
}

/* Virtual function table for multi-column menu skin */
static const menu_skin menu_skin_column =
{
	columns_get_cursor,
	display_columns,
	column_get_tag
};


/* ================== GENERIC HELPER FUNCTIONS ============== */

static bool is_valid_row(menu_type *menu, int cursor)
{
	int oid = cursor;

	if (cursor < 0 || cursor >= menu->filter_count)
		return FALSE;

	if (menu->filter_list)
		oid = menu->filter_list[cursor];

	if (!menu->row_funcs->valid_row)
		return TRUE;

	return menu->row_funcs->valid_row(menu, oid);
}

/* 
 * Return a new position in the menu based on the key
 * pressed and the flags and various handler functions.
 */
static int get_cursor_key(menu_type *menu, int top, char key)
{
	int i;
	int n = menu->filter_count;

	if (menu->flags & MN_CASELESS_TAGS)
		key = toupper((unsigned char) key);

	if (menu->flags & MN_NO_TAGS)
	{
		return -1;
	}
	else if (menu->flags & MN_REL_TAGS)
	{
		for (i = 0; i < n; i++)
		{
			char c = menu->skin->get_tag(menu, i);

			if ((menu->flags & MN_CASELESS_TAGS) && c)
				c = toupper((unsigned char) c);

			if (c && c == key)
				return i + menu->top;
		}
	}
	else if (!(menu->flags & MN_PVT_TAGS) && menu->selections)
	{
		for (i = 0; menu->selections[i]; i++)
		{
			char c = menu->selections[i];

			if (menu->flags & MN_CASELESS_TAGS)
				c = toupper((unsigned char) c);

			if (c == key)
				return i;
		}
	}
	else if (menu->row_funcs->get_tag)
	{
		for (i = 0; i < n; i++)
		{
			int oid = menu->filter_list ? menu->filter_list[i] : i;
			char c = menu->row_funcs->get_tag(menu, oid);

			if ((menu->flags & MN_CASELESS_TAGS) && c)
				c = toupper((unsigned char) c);

			if (c && c == key)
				return i;
		}
	}

	return -1;
}

/* Modal display of menu */
static void display_menu_row(menu_type *menu, int pos, int top,
                             bool cursor, int row, int col, int width)
{
	int flags = menu->flags;
	char sel = 0;
	int oid = pos;

	if (menu->filter_list)
		oid = menu->filter_list[oid];

	if (menu->row_funcs->valid_row && menu->row_funcs->valid_row(menu, oid) == 2)
		return;

	if (!(flags & MN_NO_TAGS))
	{
		if (flags & MN_REL_TAGS)
			sel = menu->skin->get_tag(menu, pos);
		else if (menu->selections && !(flags & MN_PVT_TAGS))
			sel = menu->selections[pos];
		else if (menu->row_funcs->get_tag)
			sel = menu->row_funcs->get_tag(menu, oid);
	}

	if (sel)
	{
		/* TODO: CHECK FOR VALID */
		byte color = curs_attrs[CURS_KNOWN][0 != (cursor)];
		Term_putstr(col, row, 3, color, format("%c) ", sel));
		col += 3;
		width -= 3;
	}

	menu->row_funcs->display_row(menu, oid, cursor, row, col, width);
}

void menu_refresh(menu_type *menu)
{
	int oid = menu->cursor;
	region *loc = &menu->active;

	if (!menu->filter_list)
		menu->filter_count = menu->count;

	if (menu->filter_list && menu->cursor >= 0)
		oid = menu->filter_list[oid];

	if (menu->title)
		Term_putstr(menu->boundary.col, menu->boundary.row,
				loc->width, TERM_WHITE, menu->title);

	if (menu->prompt)
		Term_putstr(loc->col, loc->row + loc->page_rows - 1,
				loc->width, TERM_WHITE, menu->prompt);

	if (menu->browse_hook && oid >= 0)
		menu->browse_hook(oid, menu->menu_data, loc);

	menu->skin->display_list(menu, menu->cursor, &menu->top, loc);
}


/*** MENU RUNNING AND INPUT HANDLING CODE ***/

/*
 * Handle mouse input in a menu.
 * 
 * Mouse output is either moving, selecting, escaping, or nothing.  Returns
 * TRUE if something changes as a result of the click.
 */
bool menu_handle_mouse(menu_type *menu, const ui_event_data *in,
		ui_event_data *out)
{
	int new_cursor;

	if (!region_inside(&menu->active, in))
	{
		/* A click to the left of the active region is 'back' */
		if (!region_inside(&menu->active, in) &&
				in->mousex < menu->active.col)
			out->type = EVT_ESCAPE;
	}
	else
	{
		new_cursor = menu->skin->get_cursor(in->mousey, in->mousex,
				menu->filter_count, menu->top, &menu->active);
	
		if (is_valid_row(menu, new_cursor))
		{
			if (new_cursor == menu->cursor || !(menu->flags & MN_DBL_TAP))
				out->type = EVT_SELECT;
			else
				out->type = EVT_MOVE;

			menu->cursor = new_cursor;
		}
	}

	return out->type != EVT_NONE;
}


/**
 * Handle any menu command keys / SELECT events.
 *
 * Returns TRUE if the key was handled at all (including if it's not handled
 * and just ignored).
 */
bool menu_handle_action(menu_type *m, const ui_event_data *in)
{
	if (m->row_funcs->row_handler)
	{
		int oid = m->cursor;
		if (m->filter_list)
			oid = m->filter_list[m->cursor];

		return m->row_funcs->row_handler(m, in, oid);
	}

	return FALSE;
}


/**
 * Handle navigation keypresses.
 *
 * Returns TRUE if they key was intelligible as navigation, regardless of
 * whether any action was taken.
 */
bool menu_handle_keypress(menu_type *menu, const ui_event_data *in,
		ui_event_data *out)
{
	bool eat = FALSE;

	/* Get the new cursor position from the menu item tags */
	int new_cursor = get_cursor_key(menu, menu->top, in->key);
	if (new_cursor >= 0 && is_valid_row(menu, new_cursor))
	{
		if (!(menu->flags & MN_DBL_TAP) || new_cursor == menu->cursor)
			out->type = EVT_SELECT;
		else
			out->type = EVT_MOVE;

		menu->cursor = new_cursor;
	}

	/* Escape stops us here */
	else if (in->key == ESCAPE)
		out->type = EVT_ESCAPE;

	/* Menus with no rows can't be navigated or used, so eat all keypresses */
	else if (menu->filter_count <= 0)
		eat = TRUE;

	/* Try existing, known keys */
	else if (in->key == ' ')
	{
		int rows = menu->active.page_rows;
		int total = menu->filter_count;

		if (rows < total)
		{
			/* Go to start of next page */
			menu->cursor += menu->active.page_rows;
			if (menu->cursor >= total - 1) menu->cursor = 0;
			menu->top = menu->cursor;
	
			out->type = EVT_MOVE;
		}
		else
		{
			eat = TRUE;
		}
	}

	else if (in->key == '\n' || in->key == '\r')
		out->type = EVT_SELECT;

	/* Try directional movement */
	else
	{
		int dir = target_dir(in->key);
	
		/* Reject diagonals */
		if (ddx[dir] && ddy[dir])
			;
	
		/* Forward/back */
		else if (ddx[dir])
			out->type = ddx[dir] < 0 ? EVT_ESCAPE : EVT_SELECT;
	
		/* Move up or down to the next valid & visible row */
		else if (ddy[dir])
		{
			int n = menu->filter_count;
			int dy = ddy[dir];
			int ind = menu->cursor + dy;

			/* Find the next valid row */
			while (!is_valid_row(menu, ind))
			{
				/* Loop around */
				if (ind > n - 1)  ind = 0;
				else if (ind < 0) ind = n - 1;
				else              ind += dy;
			}
	
			/* Set the cursor */
			menu->cursor = ind;
			assert(menu->cursor >= 0);
			assert(menu->cursor < menu->filter_count);

			out->type = EVT_MOVE;
		}
	}

	return eat;
}


/* 
 * Run a menu.
 */
ui_event_data menu_select(menu_type *menu, int notify)
{
	ui_event_data in = EVENT_EMPTY;

	assert(menu->active.width != 0 && menu->active.page_rows != 0);

	notify |= (EVT_SELECT | EVT_ESCAPE);

	/* Check for command flag */
	if (p_ptr->command_new)
	{
		Term_key_push(p_ptr->command_new);
		p_ptr->command_new = 0;
	}

	/* Stop on first unhandled event */
	while (!(in.type & notify))
	{
		bool ignore;
		ui_event_data out = EVENT_EMPTY;

		menu_refresh(menu);
		in = inkey_ex();

		/* Handle mouse & keyboard commands */
		if (in.type == EVT_MOUSE)
			ignore = menu_handle_mouse(menu, &in, &out);
		else if (in.type == EVT_KBRD)
		{
			if (menu->cmd_keys &&
					strchr(menu->cmd_keys, in.key) &&
					menu_handle_action(menu, &in))
				continue;

			ignore = menu_handle_keypress(menu, &in, &out);
		}
		else if (in.type == EVT_RESIZE)
		{
			menu_calc_size(menu);
			if (menu->row_funcs->resize)
				menu->row_funcs->resize(menu);
		}

		/* XXX should redraw menu here if cursor has moved */

		/* If we've selected an item, then send that event out */
		if (out.type == EVT_SELECT && menu_handle_action(menu, &out))
			continue;

		/* Notify about the outgoing type */
		if (notify & out.type)
			return out;
	}

	return in;
}


/* ================== MENU ACCESSORS ================ */

/**
 * Return the menu iter struct for a given iter ID.
 */
const menu_iter *menu_find_iter(menu_iter_id id)
{
	switch (id)
	{
		case MN_ITER_ACTIONS:
			return &menu_iter_actions;

		case MN_ITER_ITEMS:
			return &menu_iter_items;

		case MN_ITER_STRINGS:
			return &menu_iter_strings;
	}

	return NULL;
}

/*
 * Return the skin behaviour struct for a given skin ID.
 */
static const menu_skin *menu_find_skin(skin_id id)
{
	switch (id)
	{
		case MN_SKIN_SCROLL:
			return &menu_skin_scroll;

		case MN_SKIN_COLUMNS:
			return &menu_skin_column;
	}

	return NULL;
}


void menu_set_filter(menu_type *menu, const int filter_list[], int n)
{
	menu->filter_list = filter_list;
	menu->filter_count = n;
}

void menu_release_filter(menu_type *menu)
{
	menu->filter_list = NULL;
	menu->filter_count = menu->count;
}

/* ======================== MENU INITIALIZATION ==================== */

static bool menu_calc_size(menu_type *menu)
{
	/* Start from initial settings */
	menu->active = menu->boundary;

	/* Calculate term-relative width/height */
	if (menu->active.width <= 0 || menu->active.page_rows <= 0)
	{
		int w, h;
		Term_get_size(&w, &h);

		if (menu->active.width <= 0)
			menu->active.width = w + menu->active.width - menu->active.col;
		if (menu->active.page_rows <= 0)
			menu->active.page_rows = h + menu->active.page_rows - menu->active.row;
	}

	if (menu->title)
	{
		menu->active.row += 2;
		menu->active.page_rows -= 2;
		menu->active.col += 4;
	}

	if (menu->prompt)
	{
		if (menu->active.page_rows > 1)
			menu->active.page_rows--;
		else
		{
			int offset = strlen(menu->prompt) + 2;
			menu->active.col += offset;
			menu->active.width -= offset;
		}
	}

	return (menu->active.width > 0 && menu->active.page_rows > 0);
}

bool menu_layout(menu_type *m, const region *loc)
{
	m->boundary = *loc;
	return menu_calc_size(m);
}

void menu_setpriv(menu_type *menu, int count, void *data)
{
	menu->count = count;
	menu->menu_data = data;

	/* XXX need to take account of filter_list */
	if (menu->cursor >= menu->count)
		menu->cursor = menu->count - 1;
}

void *menu_priv(menu_type *menu)
{
	return menu->menu_data;
}

void menu_init(menu_type *menu, skin_id skin_id, const menu_iter *iter)
{
	const menu_skin *skin = menu_find_skin(skin_id);
	assert(skin && "menu skin not found!");
	assert(iter && "menu iter not found!");

	/* Wipe the struct */
	memset(menu, 0, sizeof *menu);

	/* Menu-specific initialisation */
	menu->row_funcs = iter;
	menu->skin = skin;
}
