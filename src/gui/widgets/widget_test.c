/*
 * Part of Astonia Client (c) Daniel Brockhaus. Please read license.txt.
 *
 * Widget System Integration Test
 *
 * This file tests the widget API to ensure all components compile and link correctly.
 * It creates various widgets and exercises their APIs without requiring rendering.
 */

#include <stdio.h>
#include <string.h>

#include "../../astonia.h"
#include "../widget.h"
#include "../widget_manager.h"
#include "widget_container.h"
#include "widget_button.h"
#include "widget_label.h"
#include "widget_itemslot.h"
#include "widget_progressbar.h"
#include "widget_textinput.h"
#include "widget_tooltip.h"

// Test callbacks
static void button_clicked(Widget *button, void *param)
{
	printf("Button clicked: %s\n", button->name);
}

static void textinput_submitted(Widget *input, const char *text, void *param)
{
	printf("Text submitted: %s\n", text);
}

int widget_system_test(void)
{
	Widget *container, *button, *label, *itemslot, *progressbar, *textinput, *tooltip;
	int success = 1;

	printf("=== Widget System Integration Test ===\n\n");

	// Initialize widget manager
	printf("1. Initializing widget manager...\n");
	if (widget_manager_init() != 0) {
		printf("   FAILED: widget_manager_init()\n");
		return 1;
	}
	printf("   OK\n\n");

	// Create container widget
	printf("2. Creating container widget...\n");
	container = widget_container_create(10, 10, 400, 300);
	if (!container) {
		printf("   FAILED: widget_container_create()\n");
		success = 0;
		goto cleanup;
	}
	widget_container_set_layout(container, LAYOUT_VERTICAL);
	widget_container_set_padding(container, 5);
	widget_container_set_spacing(container, 5);
	printf("   OK - Container ID: %d\n\n", container->id);

	// Create button widget
	printf("3. Creating button widget...\n");
	button = widget_button_create(0, 0, 100, 30, "Click Me");
	if (!button) {
		printf("   FAILED: widget_button_create()\n");
		success = 0;
		goto cleanup;
	}
	widget_button_set_callback(button, button_clicked, NULL);
	widget_add_child(container, button);
	printf("   OK - Button ID: %d\n\n", button->id);

	// Create label widget
	printf("4. Creating label widget...\n");
	label = widget_label_create(0, 0, 200, 20, "This is a test label");
	if (!label) {
		printf("   FAILED: widget_label_create()\n");
		success = 0;
		goto cleanup;
	}
	widget_label_set_alignment(label, LABEL_ALIGN_CENTER);
	widget_label_set_small_font(label, 1);
	widget_add_child(container, label);
	printf("   OK - Label ID: %d\n\n", label->id);

	// Create item slot widget
	printf("5. Creating item slot widget...\n");
	itemslot = widget_itemslot_create(0, 0, 40);
	if (!itemslot) {
		printf("   FAILED: widget_itemslot_create()\n");
		success = 0;
		goto cleanup;
	}
	widget_itemslot_set_item(itemslot, 123, 456, 10);
	widget_add_child(container, itemslot);
	printf("   OK - ItemSlot ID: %d\n\n", itemslot->id);

	// Create progress bar widget
	printf("6. Creating progress bar widget...\n");
	progressbar = widget_progressbar_create(0, 0, 150, 15, PROGRESSBAR_HORIZONTAL);
	if (!progressbar) {
		printf("   FAILED: widget_progressbar_create()\n");
		success = 0;
		goto cleanup;
	}
	widget_progressbar_set_range(progressbar, 75.0f, 100.0f);
	widget_add_child(container, progressbar);
	printf("   OK - ProgressBar ID: %d (%.0f%%)\n\n", progressbar->id,
	    widget_progressbar_get_percentage(progressbar) * 100.0f);

	// Create text input widget
	printf("7. Creating text input widget...\n");
	textinput = widget_textinput_create(0, 0, 200, 25);
	if (!textinput) {
		printf("   FAILED: widget_textinput_create()\n");
		success = 0;
		goto cleanup;
	}
	widget_textinput_set_placeholder(textinput, "Enter text here...");
	widget_textinput_set_submit_callback(textinput, textinput_submitted, NULL);
	widget_textinput_set_text(textinput, "Hello World");
	widget_add_child(container, textinput);
	printf("   OK - TextInput ID: %d (text: '%s')\n\n", textinput->id, widget_textinput_get_text(textinput));

	// Create tooltip widget
	printf("8. Creating tooltip widget...\n");
	tooltip = widget_tooltip_create(0, 0);
	if (!tooltip) {
		printf("   FAILED: widget_tooltip_create()\n");
		success = 0;
		goto cleanup;
	}
	widget_tooltip_set_text(tooltip, "This is a test tooltip\nWith multiple lines");
	widget_tooltip_set_delay(tooltip, 500);
	printf("   OK - Tooltip ID: %d\n\n", tooltip->id);

	// Test widget hierarchy
	printf("9. Testing widget hierarchy...\n");
	printf("   Container has %d children:\n", container->child_count);
	Widget *child = container->first_child;
	while (child) {
		printf("   - %s (ID: %d)\n", child->name, child->id);
		child = child->next_sibling;
	}
	printf("   OK\n\n");

	// Test widget visibility
	printf("10. Testing widget visibility...\n");
	widget_set_visible(button, 0);
	if (button->visible) {
		printf("   FAILED: widget_set_visible(0)\n");
		success = 0;
	} else {
		widget_set_visible(button, 1);
		if (!button->visible) {
			printf("   FAILED: widget_set_visible(1)\n");
			success = 0;
		} else {
			printf("   OK\n\n");
		}
	}

	// Test widget positioning
	printf("11. Testing widget positioning...\n");
	widget_set_position(label, 100, 200);
	if (label->x != 100 || label->y != 200) {
		printf("   FAILED: widget_set_position() - expected (100, 200), got (%d, %d)\n", label->x, label->y);
		success = 0;
	} else {
		printf("   OK\n\n");
	}

	// Test widget sizing
	printf("12. Testing widget sizing...\n");
	widget_set_size(progressbar, 300, 20);
	if (progressbar->width != 300 || progressbar->height != 20) {
		printf(
		    "   FAILED: widget_set_size() - expected (300x20), got (%dx%d)\n", progressbar->width, progressbar->height);
		success = 0;
	} else {
		printf("   OK\n\n");
	}

	// Test dirty tracking
	printf("13. Testing dirty tracking...\n");
	widget_mark_dirty(itemslot);
	if (!itemslot->dirty) {
		printf("   FAILED: widget_mark_dirty()\n");
		success = 0;
	} else {
		printf("   OK\n\n");
	}

cleanup:
	// Cleanup
	printf("14. Cleaning up...\n");
	widget_destroy(container); // Should cascade to all children
	widget_destroy(tooltip);
	widget_manager_cleanup();
	printf("   OK\n\n");

	// Report result
	if (success) {
		printf("=== ALL TESTS PASSED ===\n");
		return 0;
	} else {
		printf("=== SOME TESTS FAILED ===\n");
		return 1;
	}
}

// Optional: Main function if compiled standalone
#ifdef WIDGET_TEST_STANDALONE
int main(int argc, char **argv)
{
	return widget_system_test();
}
#endif
