/*
solitaire.c -- Klondike Solitaire for Pebble

Copyright (c) 2014 Jeffry Johnston <pebble@kidsquid.com>

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
pebble new-project solitaire
pebble build
pebble install --phone 192.168.1.108
*/
#include <pebble.h>

#define MODE_SELECT_SRC 0
#define MODE_SELECT_DEST 1
#define PILE_TABLEAU_LEFT 0
#define PILE_TABLEAU_RIGHT 6
#define PILE_TALON 7
#define PILE_FOUNDATIONS 8
#define PILE_FOUNDATION_LEFT 0
#define PILE_FOUNDATION_RIGHT 3

/******************************************************************************/
/* Globals                                                                    */
/******************************************************************************/
// game
static Window *game_window;
static Layer *game_window_layer;
static TextLayer *score_layer;
static char score_msg[32];
static int score;
static GBitmap *card_image;
static GBitmap *back_image;
static GBitmap *edge_image;
static GBitmap *selector_image;
static GBitmap *mode1_image;
static GBitmap *rank_image[13];
static GBitmap *suit_image[4];
static int seed;
static int deck[52];
static int stock_count;
static int talon;
static int talon_showing;
static int flips;
static int stock[24];
static int foundation[4];
static int tableau[7][19];
static int hidden_count[7];
static int tableau_count[7];
static int mode;
static int selection;
static int source;
static bool win;

// text area
static char* HELP_TEXT = "Controls\n\n"
				"Up: Select next card pile.\n\n"
				"Select: Begin or complete a card move.\n\n"
				"Down (short): Deal card to talon or abort a card move in progress.\n\n"
				"Down (long): Automatically move cards from tableau to foundation piles.\n\n"
				"Gameplay\n\n"
				"Due to display limitations, only the top- and bottom-most face up cards from each tableau pile are shown.\n\n"
				"Either an entire pile or the topmost card in a tableau pile may be moved, but partial pile moves are not possible.\n\n"
				"Once a card is moved to the foundation, it may not be moved back.";
static char* ABOUT_TEXT = "Klondike Solitaire\n\n"
				"Copyright (c) 2014 Jeffry Johnston <pebble@kidsquid.com>\n\n"
				"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
				"This is free software: you are free to change and redistribute it.\n"
				"There is NO WARRANTY, to the extent permitted by law.";
static Window *text_window;
static ScrollLayer *text_scroll_layer;
static Layer *text_window_layer;
static TextLayer *text_layer;
static char *text;

// menu and settings
static Window *menu_window;
static SimpleMenuLayer *simple_menu_layer;
static SimpleMenuSection menu_sections[3]; /* Game, Settings, Tools */
static SimpleMenuItem game_menu_items[2]; /* Play, Re-deal */
static SimpleMenuItem settings_menu_items[3]; /* Draw [One, Three], Flips [No Limit, One, Three], Score [Show, Hide] */
static SimpleMenuItem tools_menu_items[3]; /* Reset Score, Help, About */
static const char *draw_options[] = {"One Card", "Three Cards"};
static const char *fliplimit_options[] = {"No Limit", "Zero", "One", "Three"};
static const char *score_options[] = {"Show", "Hide"};
static int draw_setting;
static int fliplimit_setting;
static int score_setting;

/******************************************************************************/
/* Game Logic                                                                 */
/******************************************************************************/
static int get_source_card()
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "get_source_card, source=%i, draw_setting=%i, stock_count=%i, talon=%i, talon_showing=%i", source, draw_setting, stock_count, talon, talon_showing);
	if (source < 0 || source >= PILE_FOUNDATIONS) {
		return -1;
	}
	if (source == PILE_TALON) {
		if (stock_count < talon_showing + 1) {
			return -1;
		}
		return stock[talon + talon_showing];
	}
	if (tableau_count[source] == 0) {
		return -1;
	}
	return tableau[source][tableau_count[source] - 1];
}

static void tableau_flip_top_card()
{
	// flip top card
	if ((hidden_count[source] > 0) && (tableau_count[source] == hidden_count[source])) {
		--hidden_count[source];
	}
}

static void remove_source_card()
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "remove_source_card, start, draw_setting=%i, stock_count=%i, talon=%i, talon_showing=%i", draw_setting, stock_count, talon, talon_showing);
	int i;

	if (source == PILE_TALON) {
		--stock_count;
		for (i = talon + talon_showing; i < stock_count; ++i) {
			stock[i] = stock[i + 1];
		}
		if (talon > 0) {
			if (talon_showing > 0) {
				--talon_showing;
			} else {
				--talon;
			}
		}
	} else {
		--tableau_count[source];
		tableau_flip_top_card();
	}
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "remove_source_card, end, draw_setting=%i, stock_count=%i, talon=%i, talon_showing=%i", draw_setting, stock_count, talon, talon_showing);
}

static bool tableau_rules_met(int src_rank, int src_suit, bool king_allowed_on_empty)
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "tableau_rules_met, start, src_rank=%i, src_suit=%i, kaoe=%i", src_rank, src_suit, king_allowed_on_empty);
	int dest_card;
	int dest_rank;
	int dest_suit;
	if (tableau_count[selection] > 0) {
		dest_card = tableau[selection][tableau_count[selection] - 1];
		dest_rank = dest_card >> 2;
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "tableau_rules_met, tc[s]>0, dest_card=%i, dest_rank=%i", dest_card, dest_rank);
		if (src_rank == dest_rank - 1) {
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "tableau_rules_met, sr==dr-1");
			dest_suit = dest_card % 4;
			if ((src_suit >> 1) != (dest_suit >> 1)) {
				//APP_LOG(APP_LOG_LEVEL_DEBUG, "tableau_rules_met, end [A]. true");
				return true;
			}
		}
	} else {
		if (src_rank == 12 && king_allowed_on_empty) {
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "tableau_rules_met, end [B]. true");
			return true;
		}
	}
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "tableau_rules_met, end [C]. false");
	return false;
}

static bool multiple_cards_are_showing(int i)
{
	return (tableau_count[i] > 0) && (tableau_count[i] != hidden_count[i] + 1);
}

static bool can_move_single_card_to_tableau()
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_single_card_to_tableau, start");
	int src_card;
	int src_rank;
	int src_suit;

	if (selection == source) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_single_card_to_tableau, end [A], sel==src. false");
		return false;
	}
	src_card = get_source_card();
	if (src_card < 0) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_single_card_to_tableau, end [B], invalid src_card. false");
		return false;
	}
	src_rank = src_card >> 2;
	src_suit = src_card % 4;
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_single_card_to_tableau, end [C] (kinda)");
	return tableau_rules_met(src_rank, src_suit, true);
}

static bool can_move_pile_to_tableau()
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_pile_to_tableau, start");
	int src_card;
	int src_rank;
	int src_suit;

	if (selection == source || source > PILE_TABLEAU_RIGHT || !multiple_cards_are_showing(source)) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_pile_to_tableau, end [A]. false, selection=%i, source=%i", selection, source);
		return false;
	}
	src_card = tableau[source][hidden_count[source]];
	src_rank = src_card >> 2;
	src_suit = src_card % 4;
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_pile_to_tableau, end [B] (kinda)");
	return tableau_rules_met(src_rank, src_suit, hidden_count[source] > 0);
}

static bool can_move_to_tableau()
{
	return (can_move_single_card_to_tableau() || can_move_pile_to_tableau());
}

static void move_to_tableau()
{
	// move card or pile to tableau
	int i;

	if (can_move_single_card_to_tableau()) {
		// move single card
		tableau[selection][tableau_count[selection]] = get_source_card();
		++tableau_count[selection];
		remove_source_card();
	} else	if (can_move_pile_to_tableau()) {
		// source is tableau: move pile
		for (i = hidden_count[source]; i < tableau_count[source]; ++i) {
			tableau[selection][tableau_count[selection]] = tableau[source][i];
			++tableau_count[selection];
		}
		tableau_count[source] = hidden_count[source];
		tableau_flip_top_card();
	}
}

static int can_move_to_foundations()
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_to_foundations, start");
	int i;
	int src_card;
	int src_rank;
	int src_suit;
	int dest_card;
	int dest_rank;
	int dest_suit;

	src_card = get_source_card();
	if (src_card < 0) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_to_foundations, end [A]. 4 (false)");
		return PILE_FOUNDATION_RIGHT + 1;
	}
	src_rank = src_card >> 2;
	src_suit = src_card % 4;
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_to_foundations, src_rank=%i, src_suit=%i", src_rank, src_suit);

	for (i = PILE_FOUNDATION_LEFT; i <= PILE_FOUNDATION_RIGHT; ++i) {
		dest_card = foundation[i];
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_to_foundations, i=%i, dest_card=%i", i, dest_card);
		if (dest_card == -1) {
			if (src_rank != 0) {
				//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_to_foundations, continue [A]");
				continue;
			}
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_to_foundations, break [A]");
			break;
		}
		dest_suit = dest_card % 4;
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_to_foundations, dest_suit=%i", dest_suit);
		if (src_suit != dest_suit) {
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_to_foundations, continue [B]");
			continue;
		}
		dest_rank = dest_card >> 2;
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_to_foundations, dest_rank=%i", dest_rank);
		if (src_rank == dest_rank + 1) {
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_to_foundations, break [B]");
			break;
		}
	}
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "can_move_to_foundations, end [B]. i=%i", i);
	return i;
}

static bool move_to_foundation()
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "move_to_foundation, start");
	// move card to foundation
	int i;
	bool success = false;

	i = can_move_to_foundations();
	if (i <= PILE_FOUNDATION_RIGHT) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "move_to_foundation, i=%i", i);
		success = true;
		foundation[i] = get_source_card();
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "move_to_foundation, foundation[i]=%i", foundation[i]);
		remove_source_card();
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "move_to_foundation, removed source card");
		score += 5;

		// vibrate on win
		for (i = PILE_FOUNDATION_LEFT; i <= PILE_FOUNDATION_RIGHT; ++i) {
			if (foundation[i] < 48) {
				break;
			}
		}
		if (i > PILE_FOUNDATION_RIGHT) {
			vibes_short_pulse();
			win = true;
		}
	}
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "move_to_foundation, end. success=%i", success);
	return success;
}

static void automatically_move_to_foundations()
{
	int i;
	bool success;
	do {
		success = false;
		for (i = PILE_TABLEAU_LEFT; i <= PILE_TABLEAU_RIGHT; ++i) {
			if (tableau_count[i] > 0) {
				source = i;
				if (move_to_foundation()) {
					success = true;
				}
			}
		}
	} while (success);
}

static void deal_card_from_stock()
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "deal_card_from_stock, start, draw_setting=%i, stock_count=%i, talon=%i, talon_showing=%i, fliplimit_setting=%i", draw_setting, stock_count, talon, talon_showing, fliplimit_setting);
	if (stock_count > talon_showing + 1) {
		if (talon + talon_showing + 1 == stock_count) {
			if ((fliplimit_setting == 0) || (fliplimit_setting == 2 && flips < 1) || (fliplimit_setting == 3 && flips < 3)) {
				talon = 0;
				++flips;
			}
		} else {
			talon += talon_showing + 1;
		}
		if (draw_setting) {
			talon_showing = stock_count - talon - 1;
			if (talon_showing > 2) {
				talon_showing = 2;
			}
		}

	}
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "deal_card_from_stock, end, draw_setting=%i, stock_count=%i, talon=%i, talon_showing=%i", draw_setting, stock_count, talon, talon_showing);
}

/******************************************************************************/
/* Pile selection                                                             */
/******************************************************************************/
static bool source_pile_is_valid()
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "source_pile_is_valid, start");
	int saved_selection = selection;

	if (selection == PILE_TALON) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "source_pile_is_valid, end [A]. true, selection=%i", selection);
		return true;
	}
	source = selection;
	if (can_move_to_foundations() <= PILE_FOUNDATION_RIGHT) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "source_pile_is_valid, can_move_to_foundations, end [B]. true, selection=%i", selection);
		return true;
	}
	for (selection = PILE_TABLEAU_LEFT; selection <= PILE_TABLEAU_RIGHT; ++selection) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "source_pile_is_valid, selection=%i", selection);
		if (can_move_to_tableau()) {
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "source_pile_is_valid, can_move_to_tableau");
			selection = saved_selection;
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "source_pile_is_valid, end [C]. true, selection=%i", selection);
			return true;
		}
	}
	selection = saved_selection;
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "source_pile_is_valid, end [D]. false, selection=%i", selection);
	return false;
}

static void select_talon()
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "select_talon, start");
	int i;

	if (win) {
		return;
	}
	mode = MODE_SELECT_SRC;
	if (stock_count < 1) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "select_talon, stock_count=%i", stock_count);
		for (i = PILE_TABLEAU_LEFT; i <= PILE_TABLEAU_RIGHT; ++i) {
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "select_talon, i=%i", i);
			selection = i;
			if (source_pile_is_valid()) {
				//APP_LOG(APP_LOG_LEVEL_DEBUG, "select_talon, source_pile_is_valid");
				//APP_LOG(APP_LOG_LEVEL_DEBUG, "select_talon, end [A]. selection=%i", selection);
				return;
			}
		}
	}
	selection = PILE_TALON;
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "select_talon, end [B]. selection=%i", selection);
}

static bool destination_pile_is_valid()
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "destination_pile_is_valid, start");
	if (selection == PILE_FOUNDATIONS) {
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "destination_pile_is_valid, end [A] (kinda)");
		return (can_move_to_foundations() <= PILE_FOUNDATION_RIGHT);
	}
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "destination_pile_is_valid, end [B] (kinda)");
	return can_move_to_tableau();
}

static void select_next_valid_pile()
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "select_next_valid_pile, start");
	int wrapped = -1;

	if (mode == MODE_SELECT_SRC) {
		while (true) {
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "snvp [0], while. selection=%i", selection);
			++selection;
			if (selection >= PILE_FOUNDATIONS) {
				selection = PILE_TABLEAU_LEFT;
			}
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "snvp [0]. selection=%i", selection);
			if (source_pile_is_valid()) {
				//APP_LOG(APP_LOG_LEVEL_DEBUG, "snvp [0]. source_pile_is_valid");
				if (selection == PILE_TALON) {
					//APP_LOG(APP_LOG_LEVEL_DEBUG, "snvp [0]. select_talon");
					select_talon();
				}
				break;
			}
		}
	} else {
		while (true) {
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "snvp [1], while. selection=%i", selection);
			++selection;
			if (selection == PILE_TALON) {
				selection = PILE_FOUNDATIONS;
			}
			if (selection > PILE_FOUNDATIONS) {
				selection = PILE_TABLEAU_LEFT;
			}
			//APP_LOG(APP_LOG_LEVEL_DEBUG, "snvp [1]. selection=%i", selection);
			if (wrapped == selection) {
				//APP_LOG(APP_LOG_LEVEL_DEBUG, "snvp [1]. wrapped");
				mode = MODE_SELECT_SRC;
				select_talon();
				break;
			} else if (wrapped == -1) {
				//APP_LOG(APP_LOG_LEVEL_DEBUG, "snvp [1]. not wrapped");
				wrapped = selection;
			}
			if (destination_pile_is_valid()) {
				//APP_LOG(APP_LOG_LEVEL_DEBUG, "snvp [1]. destination_pile_is_valid");
				break;
			}
		}
	}
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "select_next_valid_pile, end");
}

static void select_valid_pile()
{
	if (mode == MODE_SELECT_SRC) {
		if (!source_pile_is_valid()) {
			select_next_valid_pile();
		}
	} else {
		if (!destination_pile_is_valid()) {
			select_next_valid_pile();
		}
	}
}

/******************************************************************************/
/* Game Controls                                                              */
/******************************************************************************/
static void up_click_handler(ClickRecognizerRef recognizer, void *context)
{
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "up_click_handler, start");
	// Move to next pile.
	if (win) {
		return;
	}
	select_next_valid_pile();
	layer_mark_dirty(game_window_layer);
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "up_click_handler, end");
}


static void select_click_handler(ClickRecognizerRef recognizer, void *context)
{
	// Begin or complete a move.
	if (win) {
		return;
	}
	if (mode == MODE_SELECT_SRC) {
		if (source_pile_is_valid()) {
			mode = MODE_SELECT_DEST;
			source = selection;
			selection = PILE_FOUNDATIONS;
			select_valid_pile();
		}
	} else {
		if (selection == PILE_FOUNDATIONS) {
			move_to_foundation();
			mode = MODE_SELECT_SRC;
			select_talon();
		} else {
			move_to_tableau();
			mode = MODE_SELECT_SRC;
			select_valid_pile();
		}
	}
	layer_mark_dirty(game_window_layer);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context)
{
	// Deal card to talon or abort a move in progress.
	if (win) {
		return;
	}
	if (mode == MODE_SELECT_SRC) {
		deal_card_from_stock();
	}
	select_talon();
	layer_mark_dirty(game_window_layer);
}

static void long_down_click_handler(ClickRecognizerRef recognizer, void *context)
{
	// Automatically move cards from tableau to foundation piles.
	if (win) {
		return;
	}
	automatically_move_to_foundations();
	mode = MODE_SELECT_SRC;
	select_valid_pile();
	layer_mark_dirty(game_window_layer);
}

static void click_config_provider(void *context)
{
	window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
	window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
	window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
	window_long_click_subscribe(BUTTON_ID_DOWN, 500, long_down_click_handler, NULL);
}

/******************************************************************************/
/* Game Display                                                               */
/******************************************************************************/
/* -3: draw nothing, -2: draw card back, -1: draw card frame, 0+: draw card/rank/suit */
static void draw_card(GContext *ctx, int x, int y, int card)
{
	if (card == -3) {
		return;
	}
	int rank = card >> 2;
	int suit = card % 4;
	graphics_draw_bitmap_in_rect(ctx, card_image, (GRect) { .origin = { x, y }, .size = card_image->bounds.size });
	switch (card) {
	case -2:
		graphics_draw_bitmap_in_rect(ctx, back_image, (GRect) { .origin = { 1 + x, 1 + y }, .size = back_image->bounds.size });
		break;
	case -1:
		break;
	default:
		graphics_draw_bitmap_in_rect(ctx, rank_image[rank], (GRect) { .origin = { 3 + x, 3 + y }, .size = rank_image[rank]->bounds.size });
		graphics_draw_bitmap_in_rect(ctx, suit_image[suit], (GRect) { .origin = { 3 + x, 17 + y }, .size = suit_image[suit]->bounds.size });
		break;
	}
}

static void game_window_layer_update_callback(Layer *me, GContext *ctx)
{
	int i;
	int x;
	int y;

	// erase layer
	graphics_context_set_fill_color(ctx, GColorBlack);
	graphics_fill_rect(ctx, (GRect) { .origin = { 0, 0 }, .size = { 144, 19 }}, 0, GCornerNone);
	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_fill_rect(ctx, (GRect) { .origin = { 0, 19 }, .size = { 144, 133 }}, 0, GCornerNone);

	// draw score
	if (score_setting == 0) {
		if (score < 0) {
			snprintf(score_msg, 32, "-$%i", -score);
		} else {
			snprintf(score_msg, 32, "$%i", score);
		}
		text_layer_set_text(score_layer, score_msg);
	}

	// draw stock
	draw_card(ctx, 2, 26, (stock_count > 0) ? ((talon + talon_showing < stock_count - 1) ? -2 : -1) : -3);

	// draw talon
	for (i = 0; i <= talon_showing; ++i) {
		if ((stock_count > i) && (talon + i < stock_count)) {
			draw_card(ctx, 22 + 9 * i, 26, stock[talon + i]);
		}
	}

	// draw foundations
	for (i = PILE_FOUNDATION_LEFT, x = 62; i <= PILE_FOUNDATION_RIGHT; ++i, x += 20) {
		draw_card(ctx, x, 26, foundation[i]);
	}

	// draw selector
	if (!win) {
		y = 60;
		switch (selection) {
		case 7:
			x = 23 + 9 * talon_showing;
			break;
		case 8:
			x = 93;
			break;
		default:
			y = multiple_cards_are_showing(selection) ? 147 : 113;
			x = 20 * selection + 3;
		}
		graphics_draw_bitmap_in_rect(ctx, selector_image, (GRect) { .origin = { x, y }, .size = selector_image->bounds.size });
	}

	// draw edges
	GRect bounds = edge_image->bounds;
	for (i = PILE_TABLEAU_LEFT; i <= PILE_TABLEAU_RIGHT; ++i) {
		for (int j = 0; j < hidden_count[i]; ++j) {
			graphics_draw_bitmap_in_rect(ctx, edge_image, (GRect) { .origin = { 20 * i + 2, 77 - 2 * j }, .size = bounds.size });
		}
	}

	// draw tableau
	for (i = PILE_TABLEAU_LEFT, x = 2; i <= PILE_TABLEAU_RIGHT; ++i, x += 20) {
		if (tableau_count[i] > 0) {
			draw_card(ctx, x, 79, tableau[i][hidden_count[i]]);
			if (multiple_cards_are_showing(i)) {
				draw_card(ctx, x, 113, tableau[i][tableau_count[i] - 1]);
			}
		}
	}

	// draw mode
	if (mode == MODE_SELECT_DEST) {
		graphics_draw_bitmap_in_rect(ctx, mode1_image, (GRect) { .origin = { 2, 3 }, .size = mode1_image->bounds.size });
	}
}

static void game_window_load(Window *window)
{
	game_window_layer = window_get_root_layer(window);
	layer_set_update_proc(game_window_layer, game_window_layer_update_callback);

	// load images
	card_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CARD);
	back_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACK);
	edge_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_EDGE);
	selector_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SELECTOR);
	mode1_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MODE1);
	rank_image[0] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_A);
	rank_image[1] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_2);
	rank_image[2] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_3);
	rank_image[3] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_4);
	rank_image[4] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_5);
	rank_image[5] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_6);
	rank_image[6] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_7);
	rank_image[7] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_8);
	rank_image[8] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_9);
	rank_image[9] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_10);
	rank_image[10] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_J);
	rank_image[11] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_Q);
	rank_image[12] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_K);
	suit_image[0] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SPADE);
	suit_image[1] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CLUB);
	suit_image[2] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_HEART);
	suit_image[3] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DIAMOND);

	score_layer = text_layer_create((GRect) { .origin = { 80, 0 }, .size = { 62, 17 } });
	text_layer_set_text_alignment(score_layer, GTextAlignmentRight);
	text_layer_set_font(score_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
	text_layer_set_background_color(score_layer, GColorBlack);
	text_layer_set_text_color(score_layer, GColorWhite);
	layer_add_child(game_window_layer, text_layer_get_layer(score_layer));
}

static void game_window_unload(Window *window)
{
	gbitmap_destroy(card_image);
	gbitmap_destroy(back_image);
	gbitmap_destroy(edge_image);
	gbitmap_destroy(selector_image);
	gbitmap_destroy(mode1_image);
	for (int i = 0; i < 13; ++i) {
		gbitmap_destroy(rank_image[i]);
	}
	for (int i = 0; i < 4; ++i) {
		gbitmap_destroy(suit_image[i]);
	}
	text_layer_destroy(score_layer);
}

static void play_game()
{
	game_window = window_create();
	window_set_click_config_provider(game_window, click_config_provider);
	window_set_window_handlers(game_window, (WindowHandlers) {
		.load = game_window_load,
		.unload = game_window_unload,
	});
	window_stack_push(game_window, false);
}

/******************************************************************************/
/* Game Initialization                                                        */
/******************************************************************************/
/* LCG pseudo-random number generator. Max may be 0-63. */
static int rnd(int max)
{
	int v;
	do {
		seed = (seed * 214013 + 2531011) & ((1U << 31) - 1);
		v = seed >> 26;
	} while (v > max);
	return v;
}

static void shuffle_and_deal()
{
 	int i;
 	int j;
 	int k;

 	/* shuffle */
	seed = time(NULL);
	for (i = 0; i < 52; ++i) {
		deck[i] = i;
	}
	for (i = 51; i >= 1; --i) {
		j = rnd(i);
		k = deck[j];
		deck[j] = deck[i];
		deck[i] = k;
	}

	/* deal */
	for (i = 0; i < 24; ++i) {
		stock[i] = deck[i];
		//APP_LOG(APP_LOG_LEVEL_DEBUG, "stock[%i]=%i, rank=%i, suit=%i, card=%c%c", i, stock[i], stock[i]>>2, stock[i]%4, "A23456789TJQK"[stock[i]>>2], "SCHD"[stock[i]%4]);
	}
	stock_count = 24;
	talon = 0;
	talon_showing = draw_setting ? 2 : 0;
	for (i = 0; i < 4; ++i) {
		foundation[i] = -1;
	}
	for (i = 0, k = 24; i < 7; ++i) {
		for (j = 0; j <= i; ++j, ++k) {
			tableau[i][j] = deck[k];
		}
		hidden_count[i] = i;
		tableau_count[i] = i + 1;
	}
	win = false;
	score -= 52;
	flips = 0;
	select_talon();
}

/******************************************************************************/
/* Serialization                                                              */
/******************************************************************************/
/*
	Serialization format:
	Bytes	Description		Offset
	-----	-----------		------
	1	stock_count		0
	1	talon			1
	4	foundation[0-3]		2
	7	tableau_count		6
	7	hidden_count		13
	<=52	stock, tableau[0-7]	20
	--
	72
*/
static void save_state()
{
	int i;
	int j;
	int b;
	unsigned char state[82];

	state[0] = (unsigned char)stock_count;
	for (i = 0; i < stock_count; ++i) {
		state[20 + i] = (unsigned char)stock[i];
	}
	state[1] = (unsigned char)talon;
	for (i = PILE_FOUNDATION_LEFT; i <= PILE_FOUNDATION_RIGHT; ++i) {
		state[2 + i] = (unsigned char)foundation[i];
	}
	b = 20 + stock_count;
	for (i = PILE_TABLEAU_LEFT; i <= PILE_TABLEAU_RIGHT; ++i) {
		state[6 + i] = (unsigned char)tableau_count[i];
		state[13 + i] = (unsigned char)hidden_count[i];
		for (j = 0; j < tableau_count[i]; ++j, ++b) {
			state[b] = tableau[i][j];
		}
	}
	state[72] = (unsigned char)win;
	state[73] = (unsigned char)draw_setting;
	state[74] = (unsigned char)fliplimit_setting;
	state[75] = (unsigned char)score_setting;
	state[76] = (unsigned char)flips;
	state[77] = (unsigned char)talon_showing;
	memcpy(state + 78, (char*)&score, 4);
	persist_write_data(0, state, 82);
}

static bool load_state()
{
	int i;
	int j;
	int b;
	unsigned char state[82];

	if (persist_read_data(0, state, 82) != 82) {
		return false;
	} 
	//score = persist_read_int(0);
	win = state[72];
	draw_setting = state[73];
	fliplimit_setting = state[74];
	score_setting = state[75];
	flips = state[76];
	talon_showing = state[77];
	memcpy((char*)&score, state + 78, 4);

	stock_count = state[0];
	for (i = 0; i < stock_count; ++i) {
		stock[i] = state[20 + i];
	}
	talon = state[1];
	for (i = PILE_FOUNDATION_LEFT; i <= PILE_FOUNDATION_RIGHT; ++i) {
		foundation[i] = state[2 + i];
		if (foundation[i] == 255) {
			foundation[i] = -1;
		}
	}
	b = 20 + stock_count;
	for (i = PILE_TABLEAU_LEFT; i <= PILE_TABLEAU_RIGHT; ++i) {
		tableau_count[i] = state[6 + i];
		hidden_count[i] = state[13 + i];
		for (j = 0; j < tableau_count[i]; ++j, ++b) {
			tableau[i][j] = state[b];
		}
	}
	select_talon();
	return true;
}

/******************************************************************************/
/* Menus and App Initialization                                               */
/******************************************************************************/
static void text_window_load(Window *window)
{
	text_window_layer = window_get_root_layer(window);
	text_scroll_layer = scroll_layer_create(layer_get_bounds(text_window_layer));
	scroll_layer_set_click_config_onto_window(text_scroll_layer, window);
	text_layer = text_layer_create(GRect(0, 0, 144, 2000));
	text_layer_set_text(text_layer, text);
	GSize max_size = text_layer_get_content_size(text_layer);
	text_layer_set_size(text_layer, GSize(max_size.w, max_size.h + 20));
	scroll_layer_set_content_size(text_scroll_layer, GSize(144, max_size.h + 20));
	scroll_layer_add_child(text_scroll_layer, text_layer_get_layer(text_layer));
	layer_add_child(text_window_layer, scroll_layer_get_layer(text_scroll_layer));
}

static void text_window_unload(Window *window)
{
	text_layer_destroy(text_layer);
	scroll_layer_destroy(text_scroll_layer);
	window_destroy(text_window);
}

static void text_area()
{
	text_window = window_create();
	window_set_window_handlers(text_window, (WindowHandlers) {
		.load = text_window_load,
		.unload = text_window_unload,
	});
	window_stack_push(text_window, false);
}

static void game_menu_select_callback(int index, void *ctx)
{
	switch (index) {
	case 0:
		// Play
		play_game();
		break;
	case 1:
		// Re-deal
		shuffle_and_deal();
		play_game();
		break;
	}
	layer_mark_dirty(simple_menu_layer_get_layer(simple_menu_layer));
}

static void settings_menu_select_callback(int index, void *ctx)
{
	switch (index) {
	case 0:
		// Draw
		if (draw_setting == 0) {
			draw_setting = 1;
			talon_showing = stock_count - talon - 1;
			if (talon_showing > 2) {
				talon_showing = 2;
			}
		} else {
			draw_setting = 0;
			talon_showing = 0;
		}
		settings_menu_items[0].subtitle = draw_options[draw_setting];
		break;
	case 1:
		// Flips
		fliplimit_setting = (fliplimit_setting + 1) % 4;
		settings_menu_items[1].subtitle = fliplimit_options[fliplimit_setting];
		break;
	case 2:
		// Score
		score_setting = (score_setting + 1) % 2;
		settings_menu_items[2].subtitle = score_options[score_setting];
		break;
	}
	layer_mark_dirty(simple_menu_layer_get_layer(simple_menu_layer));
}

static void tools_menu_select_callback(int index, void *ctx)
{
	switch (index) {
	case 0:
		// Reset Score
		score = 0;
		break;
	case 1:
		// Help
		text = HELP_TEXT;
		text_area();
		break;
	case 2:
		// About
		text = ABOUT_TEXT;
		text_area();
		break;
	}
	layer_mark_dirty(simple_menu_layer_get_layer(simple_menu_layer));
}

static void menu_window_load(Window *window)
{
	game_menu_items[0] = (SimpleMenuItem){
		.title = "Play",
		.callback = game_menu_select_callback,
	};
	game_menu_items[1] = (SimpleMenuItem){
		.title = "Re-deal",
		.callback = game_menu_select_callback,
	};

	settings_menu_items[0] = (SimpleMenuItem){
		.title = "Draw",
		.subtitle = draw_options[draw_setting],
		.callback = settings_menu_select_callback,
	};
	settings_menu_items[1] = (SimpleMenuItem){
		.title = "Flip Limit",
		.subtitle = fliplimit_options[fliplimit_setting],
		.callback = settings_menu_select_callback,
	};
	settings_menu_items[2] = (SimpleMenuItem){
		.title = "Score",
		.subtitle = score_options[score_setting],
		.callback = settings_menu_select_callback,
	};

	tools_menu_items[0] = (SimpleMenuItem){
		.title = "Reset Score",
		.callback = tools_menu_select_callback,
	};
	tools_menu_items[1] = (SimpleMenuItem){
		.title = "Help",
		.callback = tools_menu_select_callback,
	};
	tools_menu_items[2] = (SimpleMenuItem){
		.title = "About",
		.callback = tools_menu_select_callback,
	};

	menu_sections[0] = (SimpleMenuSection){
		.title = "Game",
		.num_items = 2,
		.items = game_menu_items,
	};
	menu_sections[1] = (SimpleMenuSection){
		.title = "Settings",
		.num_items = 3,
		.items = settings_menu_items,
	};
	menu_sections[2] = (SimpleMenuSection){
		.title = "Tools",
		.num_items = 3,
		.items = tools_menu_items,
	};

	Layer *window_layer = window_get_root_layer(window);
	simple_menu_layer = simple_menu_layer_create(layer_get_frame(window_layer), window, menu_sections, 3, NULL);
	layer_add_child(window_layer, simple_menu_layer_get_layer(simple_menu_layer));
}

static void menu_window_unload(Window *window)
{
	simple_menu_layer_destroy(simple_menu_layer);
}

static void init(void)
{
	if (!load_state()) {
		score = 0;
		shuffle_and_deal();
	}

	menu_window = window_create();
	window_set_window_handlers(menu_window, (WindowHandlers) {
		.load = menu_window_load,
		.unload = menu_window_unload,
	});
	window_stack_push(menu_window, false);
}

static void deinit(void)
{
	save_state();
	window_destroy(game_window);
}

int main(void)
{
	init();
	app_event_loop();
	deinit();
	return 0;
}

