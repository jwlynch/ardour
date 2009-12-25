/*
    Copyright (C) 2000-2006 Paul Davis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <cmath>
#include <algorithm>

#include <sigc++/bind.h>

#include "pbd/convert.h"
#include "pbd/enumwriter.h"
#include "pbd/replace_all.h"

#include <gtkmm2ext/gtk_ui.h>
#include <gtkmm2ext/utils.h>
#include <gtkmm2ext/choice.h>
#include <gtkmm2ext/stop_signal.h>
#include <gtkmm2ext/doi.h>
#include <gtkmm2ext/slider_controller.h>
#include <gtkmm2ext/bindable_button.h>

#include "ardour/ardour.h"
#include "ardour/amp.h"
#include "ardour/session.h"
#include "ardour/audioengine.h"
#include "ardour/internal_send.h"
#include "ardour/route.h"
#include "ardour/route_group.h"
#include "ardour/audio_track.h"
#include "ardour/audio_diskstream.h"
#include "ardour/panner.h"
#include "ardour/send.h"
#include "ardour/processor.h"
#include "ardour/profile.h"
#include "ardour/ladspa_plugin.h"
#include "ardour/user_bundle.h"

#include "ardour_ui.h"
#include "ardour_dialog.h"
#include "mixer_strip.h"
#include "mixer_ui.h"
#include "keyboard.h"
#include "public_editor.h"
#include "send_ui.h"
#include "io_selector.h"
#include "utils.h"
#include "gui_thread.h"
#include "route_group_menu.h"

#include "i18n.h"

using namespace ARDOUR;
using namespace PBD;
using namespace Gtk;
using namespace Gtkmm2ext;
using namespace std;

sigc::signal<void,boost::shared_ptr<Route> > MixerStrip::SwitchIO;

int MixerStrip::scrollbar_height = 0;
PBD::Signal1<void,MixerStrip*> MixerStrip::CatchDeletion;

MixerStrip::MixerStrip (Mixer_UI& mx, Session* sess, bool in_mixer)
	: AxisView(sess)
	, RouteUI (sess)
	,_mixer(mx)
	, _mixer_owned (in_mixer)
	, processor_box (sess, boost::bind (&MixerStrip::plugin_selector, this), mx.selection(), this, in_mixer)
	, gpm (sess, 250)
	, panners (sess)
	, _mono_button (_("Mono"))
	, button_table (3, 2)
	, middle_button_table (1, 2)
	, bottom_button_table (1, 2)
	, meter_point_label (_("pre"))
	, comment_button (_("Comments"))
{
	init ();

	if (!_mixer_owned) {
		/* the editor mixer strip: don't destroy it every time
		   the underlying route goes away.
		*/

		self_destruct = false;
	}
}

MixerStrip::MixerStrip (Mixer_UI& mx, Session* sess, boost::shared_ptr<Route> rt, bool in_mixer)
	: AxisView(sess)
	, RouteUI (sess)
	,_mixer(mx)
	, _mixer_owned (in_mixer)
	, processor_box (sess, sigc::mem_fun(*this, &MixerStrip::plugin_selector), mx.selection(), this, in_mixer)
	, gpm (sess, 250)
	, panners (sess)
	, button_table (3, 2)
	, middle_button_table (1, 2)
	, bottom_button_table (1, 2)
	, meter_point_label (_("pre"))
	, comment_button (_("Comments"))
{
	init ();
	set_button_names ();
	set_route (rt);
}

void
MixerStrip::init ()
{
	input_selector = 0;
	output_selector = 0;
	group_menu = 0;
	_marked_for_display = false;
	route_ops_menu = 0;
	ignore_comment_edit = false;
	ignore_toggle = false;
	comment_window = 0;
	comment_area = 0;
	_width_owner = 0;
	spacer = 0;

	/* the length of this string determines the width of the mixer strip when it is set to `wide' */
	longest_label = "longest label";

	Gtk::Image* img;

	img = manage (new Gtk::Image (::get_icon("strip_width")));
	img->show ();

	width_button.add (*img);

	img = manage (new Gtk::Image (::get_icon("hide")));
	img->show ();

	hide_button.add (*img);

	input_label.set_text (_("Input"));
	ARDOUR_UI::instance()->set_tip (&input_button, _("Button 1 to choose inputs from a port matrix, button 3 to select inputs from a menu"), "");
	input_button.add (input_label);
	input_button.set_name ("MixerIOButton");
	input_label.set_name ("MixerIOButtonLabel");

	Gtkmm2ext::set_size_request_to_display_given_text (input_button, longest_label.c_str(), 4, 4);

	output_label.set_text (_("Output"));
	ARDOUR_UI::instance()->set_tip (&output_button, _("Button 1 to choose outputs from a port matrix, button 3 to select inputs from a menu"), "");
	output_button.add (output_label);
	output_button.set_name ("MixerIOButton");
	output_label.set_name ("MixerIOButtonLabel");
	Gtkmm2ext::set_size_request_to_display_given_text (output_button, longest_label.c_str(), 4, 4);

	ARDOUR_UI::instance()->set_tip (&meter_point_button, _("Select metering point"), "");
	meter_point_button.add (meter_point_label);
	meter_point_button.set_name ("MixerStripMeterPreButton");
	meter_point_label.set_name ("MixerStripMeterPreButton");

	/* TRANSLATORS: this string should be longest of the strings
	   used to describe meter points. In english, it's "input".
	*/
	set_size_request_to_display_given_text (meter_point_button, _("tupni"), 5, 5);

	bottom_button_table.attach (meter_point_button, 1, 2, 0, 1);

	meter_point_button.signal_button_press_event().connect (sigc::mem_fun (gpm, &GainMeter::meter_press), false);
	meter_point_button.signal_button_release_event().connect (sigc::mem_fun (gpm, &GainMeter::meter_release), false);

	hide_button.set_events (hide_button.get_events() & ~(Gdk::ENTER_NOTIFY_MASK|Gdk::LEAVE_NOTIFY_MASK));

	mute_button->set_name ("MixerMuteButton");
	solo_button->set_name ("MixerSoloButton");

	button_table.set_homogeneous (true);
	button_table.set_spacings (0);

	button_table.attach (name_button, 0, 2, 0, 1);
	button_table.attach (input_button, 0, 2, 1, 2);

	middle_button_table.set_homogeneous (true);
	middle_button_table.set_spacings (0);
	middle_button_table.attach (*mute_button, 0, 1, 0, 1);
	middle_button_table.attach (*solo_button, 1, 2, 0, 1);

	bottom_button_table.set_col_spacings (0);
	bottom_button_table.set_homogeneous (true);
	bottom_button_table.attach (group_button, 0, 1, 0, 1);

	name_button.add (name_label);
	name_button.set_name ("MixerNameButton");
	Gtkmm2ext::set_size_request_to_display_given_text (name_button, longest_label.c_str(), 2, 2);

	name_label.set_name ("MixerNameButtonLabel");
	ARDOUR_UI::instance()->set_tip (&group_button, _("Mix group"), "");
	group_button.add (group_label);
	group_button.set_name ("MixerGroupButton");
	group_label.set_name ("MixerGroupButtonLabel");

	comment_button.set_name ("MixerCommentButton");

	comment_button.signal_clicked().connect (sigc::mem_fun(*this, &MixerStrip::comment_button_clicked));

	global_vpacker.set_border_width (0);
	global_vpacker.set_spacing (0);

	width_button.set_name ("MixerWidthButton");
	hide_button.set_name ("MixerHideButton");
	top_event_box.set_name ("MixerTopEventBox");

	width_button.signal_clicked().connect (sigc::mem_fun(*this, &MixerStrip::width_clicked));
	hide_button.signal_clicked().connect (sigc::mem_fun(*this, &MixerStrip::hide_clicked));

	width_hide_box.pack_start (width_button, false, true);
	width_hide_box.pack_start (top_event_box, true, true);
	width_hide_box.pack_end (hide_button, false, true);
	gain_meter_alignment.set_padding(0, 4, 0, 0);
	gain_meter_alignment.add(gpm);

	whvbox.pack_start (width_hide_box, true, true);

	global_vpacker.pack_start (whvbox, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (button_table,Gtk::PACK_SHRINK);
	global_vpacker.pack_start (processor_box, true, true);
	global_vpacker.pack_start (middle_button_table,Gtk::PACK_SHRINK);
	global_vpacker.pack_start (gain_meter_alignment,Gtk::PACK_SHRINK);
	global_vpacker.pack_start (bottom_button_table,Gtk::PACK_SHRINK);
	if (!is_midi_track()) {
		global_vpacker.pack_start (panners, Gtk::PACK_SHRINK);
	}
	global_vpacker.pack_start (_mono_button, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (output_button, Gtk::PACK_SHRINK);
	global_vpacker.pack_start (comment_button, Gtk::PACK_SHRINK);

	global_frame.add (global_vpacker);
	global_frame.set_shadow_type (Gtk::SHADOW_IN);
	global_frame.set_name ("BaseFrame");

	add (global_frame);

	/* force setting of visible selected status */

	_selected = true;
	set_selected (false);

	_packed = false;
	_embedded = false;

	_session->engine().Stopped.connect (*this, boost::bind (&MixerStrip::engine_stopped, this), gui_context());
	_session->engine().Running.connect (*this, boost::bind (&MixerStrip::engine_running, this), gui_context());

	input_button.signal_button_press_event().connect (sigc::mem_fun(*this, &MixerStrip::input_press), false);
	output_button.signal_button_press_event().connect (sigc::mem_fun(*this, &MixerStrip::output_press), false);

	solo_button->signal_button_press_event().connect (sigc::mem_fun(*this, &RouteUI::solo_press), false);
	solo_button->signal_button_release_event().connect (sigc::mem_fun(*this, &RouteUI::solo_release), false);
	mute_button->signal_button_press_event().connect (sigc::mem_fun(*this, &RouteUI::mute_press), false);
	mute_button->signal_button_release_event().connect (sigc::mem_fun(*this, &RouteUI::mute_release), false);

	/* we don't need this if its not an audio track, but we don't know that yet and it doesn't
	   hurt (much).
	*/

	rec_enable_button->set_name ("MixerRecordEnableButton");
	rec_enable_button->signal_button_press_event().connect (sigc::mem_fun(*this, &RouteUI::rec_enable_press), false);
	rec_enable_button->signal_button_release_event().connect (sigc::mem_fun(*this, &RouteUI::rec_enable_release));

	/* ditto for this button and busses */

	show_sends_button->signal_button_press_event().connect (sigc::mem_fun(*this, &RouteUI::show_sends_press), false);
	show_sends_button->signal_button_release_event().connect (sigc::mem_fun(*this, &RouteUI::show_sends_release));

	name_button.signal_button_press_event().connect (sigc::mem_fun(*this, &MixerStrip::name_button_button_press), false);
	group_button.signal_button_press_event().connect (sigc::mem_fun(*this, &MixerStrip::select_route_group), false);

	_width = (Width) -1;

	/* start off as a passthru strip. we'll correct this, if necessary,
	   in update_diskstream_display().
	*/

	/* start off as a passthru strip. we'll correct this, if necessary,
	   in update_diskstream_display().
	*/

	if (is_midi_track())
		set_name ("MidiTrackStripBase");
	else
		set_name ("AudioTrackStripBase");

	add_events (Gdk::BUTTON_RELEASE_MASK|
		    Gdk::ENTER_NOTIFY_MASK|
		    Gdk::LEAVE_NOTIFY_MASK|
		    Gdk::KEY_PRESS_MASK|
		    Gdk::KEY_RELEASE_MASK);

	set_flags (get_flags() | Gtk::CAN_FOCUS);

	SwitchIO.connect (sigc::mem_fun (*this, &MixerStrip::switch_io));
}

MixerStrip::~MixerStrip ()
{
	CatchDeletion (this);

	delete input_selector;
	delete output_selector;
	delete comment_window;
}

void
MixerStrip::set_route (boost::shared_ptr<Route> rt)
{
	if (rec_enable_button->get_parent()) {
		button_table.remove (*rec_enable_button);
	}

	if (show_sends_button->get_parent()) {
		button_table.remove (*show_sends_button);
	}

	RouteUI::set_route (rt);

	delete input_selector;
	input_selector = 0;

	delete output_selector;
	output_selector = 0;

	revert_to_default_display ();

	processor_box.set_route (rt);

	if (set_color_from_route()) {
		set_color (unique_random_color());
	}

	if (_mixer_owned && (route()->is_master() || route()->is_control())) {

		if (scrollbar_height == 0) {
			HScrollbar scrollbar;
			Gtk::Requisition requisition(scrollbar.size_request ());
			scrollbar_height = requisition.height;
		}

		spacer = manage (new EventBox);
		spacer->set_size_request (-1, scrollbar_height);
		global_vpacker.pack_start (*spacer, false, false);
	}

	if (is_audio_track()) {

		boost::shared_ptr<AudioTrack> at = audio_track();

		at->FreezeChange.connect (route_connections, boost::bind (&MixerStrip::map_frozen, this), gui_context());

		button_table.attach (*rec_enable_button, 0, 2, 2, 3);
		rec_enable_button->set_sensitive (_session->writable());
		rec_enable_button->show();

	} else if (!is_track()) {
		/* non-master bus */

		if (!_route->is_master()) {
			button_table.attach (*show_sends_button, 0, 2, 2, 3);
			show_sends_button->show();
		}
	}

	if (_route->phase_invert()) {
	        name_label.set_text (X_("Ø ") + name_label.get_text());
	} else {
	        name_label.set_text (_route->name());
	}

	_mono_button.set_name ("MixerMonoButton");
	_mono_button.signal_clicked().connect (sigc::mem_fun (*this, &MixerStrip::mono_button_clicked));

	switch (_route->meter_point()) {
	case MeterInput:
		meter_point_label.set_text (_("input"));
		break;

	case MeterPreFader:
		meter_point_label.set_text (_("pre"));
		break;

	case MeterPostFader:
		meter_point_label.set_text (_("post"));
		break;

	case MeterCustom:
		meter_point_label.set_text (_("custom"));
		break;
	}

	delete route_ops_menu;
	route_ops_menu = 0;

	ARDOUR_UI::instance()->tooltips().set_tip (comment_button, _route->comment().empty() ?
						   _("Click to Add/Edit Comments"):
						   _route->comment());

	_route->meter_change.connect (route_connections, ui_bind (&MixerStrip::meter_changed, this, _1), gui_context());
	_route->input()->changed.connect (route_connections, ui_bind (&MixerStrip::input_changed, this, _1, _2), gui_context());
	_route->output()->changed.connect (route_connections, ui_bind (&MixerStrip::output_changed, this, _1, _2), gui_context());
	_route->route_group_changed.connect (route_connections, boost::bind (&MixerStrip::route_group_changed, this), gui_context());

	if (_route->panner()) {
		_route->panner()->Changed.connect (route_connections, boost::bind (&MixerStrip::connect_to_pan, this), gui_context());
	}

	if (is_audio_track()) {
		audio_track()->DiskstreamChanged.connect (route_connections, boost::bind (&MixerStrip::diskstream_changed, this), gui_context());
	}

	_route->NameChanged.connect (route_connections, boost::bind (&RouteUI::name_changed, this), gui_context());
	_route->comment_changed.connect (route_connections, ui_bind (&MixerStrip::comment_changed, this, _1), gui_context());
	_route->gui_changed.connect (route_connections, ui_bind (&MixerStrip::route_gui_changed, this, _1, _2), gui_context());

	set_stuff_from_route ();

	/* now force an update of all the various elements */

	mute_changed (0);
	solo_changed (0);
	name_changed ();
	comment_changed (0);
	route_group_changed ();

	connect_to_pan ();

	panners.setup_pan ();

	update_diskstream_display ();
	update_input_display ();
	update_output_display ();

	add_events (Gdk::BUTTON_RELEASE_MASK);

	processor_box.show();

	if (!route()->is_master() && !route()->is_control()) {
		/* we don't allow master or control routes to be hidden */
		hide_button.show();
	}

	width_button.show();
	width_hide_box.show();
	whvbox.show ();
	global_frame.show();
	global_vpacker.show();
	button_table.show();
	middle_button_table.show();
	bottom_button_table.show();
	processor_box.show_all ();
	gpm.show_all ();
	panners.show_all ();
	gain_meter_alignment.show ();
	gain_unit_button.show();
	gain_unit_label.show();
	meter_point_button.show();
	meter_point_label.show();
	diskstream_button.show();
	diskstream_label.show();
	input_button.show();
	input_label.show();
	output_button.show();
	output_label.show();
	name_label.show();
	name_button.show();
	comment_button.show();
	group_button.show();
	group_label.show();

	show ();
}

void
MixerStrip::set_stuff_from_route ()
{
	XMLProperty *prop;

	ensure_xml_node ();

	/* if width is not set, it will be set by the MixerUI or editor */

	if ((prop = xml_node->property ("strip-width")) != 0) {
		set_width_enum (Width (string_2_enum (prop->value(), _width)), this);
	}

	if ((prop = xml_node->property ("shown-mixer")) != 0) {
		if (prop->value() == "no") {
			_marked_for_display = false;
		} else {
			_marked_for_display = true;
		}
	} else {
		/* backwards compatibility */
		_marked_for_display = true;
	}
}

void
MixerStrip::set_width_enum (Width w, void* owner)
{
	/* always set the gpm width again, things may be hidden */

	gpm.set_width (w);
	panners.set_width (w);

	boost::shared_ptr<AutomationList> gain_automation = _route->gain_control()->alist();

	_width_owner = owner;

	ensure_xml_node ();

	_width = w;

	if (_width_owner == this) {
		xml_node->add_property ("strip-width", enum_2_string (_width));
	}

	set_button_names ();

	switch (w) {
	case Wide:
		if (show_sends_button)  {
			((Gtk::Label*)show_sends_button->get_child())->set_text (_("Sends"));
		}

		if (_route->comment() == "") {
			comment_button.unset_bg (STATE_NORMAL);
			((Gtk::Label*)comment_button.get_child())->set_text (_("Comments"));
		} else {
			comment_button.modify_bg (STATE_NORMAL, color());
			((Gtk::Label*)comment_button.get_child())->set_text (_("*Comments*"));
		}

		((Gtk::Label*)gpm.gain_automation_style_button.get_child())->set_text (
				gpm.astyle_string(gain_automation->automation_style()));
		((Gtk::Label*)gpm.gain_automation_state_button.get_child())->set_text (
				gpm.astate_string(gain_automation->automation_state()));

		if (_route->panner()) {
			((Gtk::Label*)panners.pan_automation_style_button.get_child())->set_text (
					panners.astyle_string(_route->panner()->automation_style()));
			((Gtk::Label*)panners.pan_automation_state_button.get_child())->set_text (
					panners.astate_string(_route->panner()->automation_state()));
		}

		Gtkmm2ext::set_size_request_to_display_given_text (name_button, "long", 2, 2);
		set_size_request (-1, -1);
		break;

	case Narrow:
		if (show_sends_button) {
			((Gtk::Label*)show_sends_button->get_child())->set_text (_("Snd"));
		}

		if (_route->comment() == "") {
		       comment_button.unset_bg (STATE_NORMAL);
		       ((Gtk::Label*)comment_button.get_child())->set_text (_("Cmt"));
		} else {
		       comment_button.modify_bg (STATE_NORMAL, color());
		       ((Gtk::Label*)comment_button.get_child())->set_text (_("*Cmt*"));
		}

		((Gtk::Label*)gpm.gain_automation_style_button.get_child())->set_text (
				gpm.short_astyle_string(gain_automation->automation_style()));
		((Gtk::Label*)gpm.gain_automation_state_button.get_child())->set_text (
				gpm.short_astate_string(gain_automation->automation_state()));

		if (_route->panner()) {
			((Gtk::Label*)panners.pan_automation_style_button.get_child())->set_text (
			panners.short_astyle_string(_route->panner()->automation_style()));
			((Gtk::Label*)panners.pan_automation_state_button.get_child())->set_text (
			panners.short_astate_string(_route->panner()->automation_state()));
		}

		Gtkmm2ext::set_size_request_to_display_given_text (name_button, longest_label.c_str(), 2, 2);
		set_size_request (max (50, gpm.get_gm_width()), -1);
		break;
	}

	processor_box.set_width (w);
	
	update_input_display ();
	update_output_display ();
	route_group_changed ();
	name_changed ();
	WidthChanged ();
}

void
MixerStrip::set_packed (bool yn)
{
	_packed = yn;

	ensure_xml_node ();

	if (_packed) {
		xml_node->add_property ("shown-mixer", "yes");
	} else {
		xml_node->add_property ("shown-mixer", "no");
	}
}


gint
MixerStrip::output_press (GdkEventButton *ev)
{
        using namespace Menu_Helpers;
	if (!_session->engine().connected()) {
	        MessageDialog msg (_("Not connected to JACK - no I/O changes are possible"));
		msg.run ();
		return true;
	}

	MenuList& citems = output_menu.items();
	switch (ev->button) {

	case 1:
		edit_output_configuration ();
		break;

	case 3:
	{
		output_menu.set_name ("ArdourContextMenu");
		citems.clear ();
		output_menu_bundles.clear ();

		citems.push_back (MenuElem (_("Disconnect"), sigc::mem_fun (*(static_cast<RouteUI*>(this)), &RouteUI::disconnect_output)));
		citems.push_back (SeparatorElem());

		ARDOUR::BundleList current = _route->output()->bundles_connected ();

		boost::shared_ptr<ARDOUR::BundleList> b = _session->bundles ();

		/* give user bundles first chance at being in the menu */
		
		for (ARDOUR::BundleList::iterator i = b->begin(); i != b->end(); ++i) {
			if (boost::dynamic_pointer_cast<UserBundle> (*i)) {
				maybe_add_bundle_to_output_menu (*i, current);
			}
		}

		for (ARDOUR::BundleList::iterator i = b->begin(); i != b->end(); ++i) {
			if (boost::dynamic_pointer_cast<UserBundle> (*i) == 0) {
				maybe_add_bundle_to_output_menu (*i, current);
			}
		}
		
		boost::shared_ptr<ARDOUR::RouteList> routes = _session->get_routes ();
		for (ARDOUR::RouteList::const_iterator i = routes->begin(); i != routes->end(); ++i) {
			maybe_add_bundle_to_output_menu ((*i)->input()->bundle(), current);
		}

		if (citems.size() == 2) {
			/* no routes added; remove the separator */
			citems.pop_back ();
		}

		output_menu.popup (1, ev->time);
		break;
	}

	default:
	        break;
	}
	return TRUE;
}

void
MixerStrip::edit_output_configuration ()
{
	if (output_selector == 0) {
		
		boost::shared_ptr<Send> send;
		boost::shared_ptr<IO> output;

		if ((send = boost::dynamic_pointer_cast<Send>(_current_delivery)) != 0) {
			if (!boost::dynamic_pointer_cast<InternalSend>(send)) {
				output = send->output();
			} else {
				output = _route->output ();
			} 
		} else {
			output = _route->output ();
		}
		
		output_selector = new IOSelectorWindow (_session, output);
	}

	if (output_selector->is_visible()) {
		output_selector->get_toplevel()->get_window()->raise();
	} else {
		output_selector->present ();
	}
}

void
MixerStrip::edit_input_configuration ()
{
	if (input_selector == 0) {
		input_selector = new IOSelectorWindow (_session, _route->input());
	}

	if (input_selector->is_visible()) {
		input_selector->get_toplevel()->get_window()->raise();
	} else {
		input_selector->present ();
	}
}

gint
MixerStrip::input_press (GdkEventButton *ev)
{
	using namespace Menu_Helpers;

	MenuList& citems = input_menu.items();
	input_menu.set_name ("ArdourContextMenu");
	citems.clear();

	if (!_session->engine().connected()) {
	        MessageDialog msg (_("Not connected to JACK - no I/O changes are possible"));
		msg.run ();
		return true;
	}

	switch (ev->button) {

	case 1:
		edit_input_configuration ();
		break;

	case 3:
	{
		citems.push_back (MenuElem (_("Disconnect"), sigc::mem_fun (*(static_cast<RouteUI*>(this)), &RouteUI::disconnect_input)));
		citems.push_back (SeparatorElem());
		input_menu_bundles.clear ();

		ARDOUR::BundleList current = _route->input()->bundles_connected ();

		boost::shared_ptr<ARDOUR::BundleList> b = _session->bundles ();

		/* give user bundles first chance at being in the menu */
		
		for (ARDOUR::BundleList::iterator i = b->begin(); i != b->end(); ++i) {
			if (boost::dynamic_pointer_cast<UserBundle> (*i)) {
				maybe_add_bundle_to_input_menu (*i, current);
			}
		}

		for (ARDOUR::BundleList::iterator i = b->begin(); i != b->end(); ++i) {
			if (boost::dynamic_pointer_cast<UserBundle> (*i) == 0) {
				maybe_add_bundle_to_input_menu (*i, current);
			}
		}
		
		boost::shared_ptr<ARDOUR::RouteList> routes = _session->get_routes ();
		for (ARDOUR::RouteList::const_iterator i = routes->begin(); i != routes->end(); ++i) {
			maybe_add_bundle_to_input_menu ((*i)->output()->bundle(), current);
		}

		if (citems.size() == 2) {
			/* no routes added; remove the separator */
			citems.pop_back ();
		}

		input_menu.popup (1, ev->time);
		break;
	}
	default:
	        break;
	}
	return TRUE;
}

void
MixerStrip::bundle_input_toggled (boost::shared_ptr<ARDOUR::Bundle> c)
{
	if (ignore_toggle) {
		return;
	}

	ARDOUR::BundleList current = _route->input()->bundles_connected ();

	if (std::find (current.begin(), current.end(), c) == current.end()) {
		_route->input()->connect_ports_to_bundle (c, this);
	} else {
		_route->input()->disconnect_ports_from_bundle (c, this);
	}
}

void
MixerStrip::bundle_output_toggled (boost::shared_ptr<ARDOUR::Bundle> c)
{
	if (ignore_toggle) {
		return;
	}

	ARDOUR::BundleList current = _route->output()->bundles_connected ();

	if (std::find (current.begin(), current.end(), c) == current.end()) {
		_route->output()->connect_ports_to_bundle (c, this);
	} else {
		_route->output()->disconnect_ports_from_bundle (c, this);
	}
}

void
MixerStrip::maybe_add_bundle_to_input_menu (boost::shared_ptr<Bundle> b, ARDOUR::BundleList const & current)
{
	using namespace Menu_Helpers;

 	if (b->ports_are_outputs() == false ||
	    route()->input()->default_type() != b->type() ||
	    b->nchannels() != _route->n_inputs().get (b->type ())) {

 		return;
 	}

	list<boost::shared_ptr<Bundle> >::iterator i = input_menu_bundles.begin ();
	while (i != input_menu_bundles.end() && b->has_same_ports (*i) == false) {
		++i;
	}

	if (i != input_menu_bundles.end()) {
		return;
	}

	input_menu_bundles.push_back (b);

	MenuList& citems = input_menu.items();

	std::string n = b->name ();
	replace_all (n, "_", " ");

	citems.push_back (CheckMenuElem (n, sigc::bind (sigc::mem_fun(*this, &MixerStrip::bundle_input_toggled), b)));

	if (std::find (current.begin(), current.end(), b) != current.end()) {
		ignore_toggle = true;
		dynamic_cast<CheckMenuItem *> (&citems.back())->set_active (true);
		ignore_toggle = false;
	}
}

void
MixerStrip::maybe_add_bundle_to_output_menu (boost::shared_ptr<Bundle> b, ARDOUR::BundleList const & current)
{
	using namespace Menu_Helpers;

 	if (b->ports_are_inputs() == false ||
	    route()->output()->default_type() != b->type() ||
	    b->nchannels() != _route->n_outputs().get (b->type ())) {

 		return;
 	}

	list<boost::shared_ptr<Bundle> >::iterator i = output_menu_bundles.begin ();
	while (i != output_menu_bundles.end() && b->has_same_ports (*i) == false) {
		++i;
	}

	if (i != output_menu_bundles.end()) {
		return;
	}

	output_menu_bundles.push_back (b);
	
	MenuList& citems = output_menu.items();

	std::string n = b->name ();
	replace_all (n, "_", " ");

	citems.push_back (CheckMenuElem (n, sigc::bind (sigc::mem_fun(*this, &MixerStrip::bundle_output_toggled), b)));

	if (std::find (current.begin(), current.end(), b) != current.end()) {
		ignore_toggle = true;
		dynamic_cast<CheckMenuItem *> (&citems.back())->set_active (true);
		ignore_toggle = false;
	}
}

void
MixerStrip::update_diskstream_display ()
{
	if (is_track()) {

		if (input_selector) {
			input_selector->hide_all ();
		}

		show_route_color ();

	} else {

		show_passthru_color ();
	}
}

void
MixerStrip::connect_to_pan ()
{
	ENSURE_GUI_THREAD (*this, &MixerStrip::connect_to_pan)

	panstate_connection.disconnect ();
	panstyle_connection.disconnect ();

	if (!_route->panner()) {
		return;
	}

	boost::shared_ptr<ARDOUR::AutomationControl> pan_control
		= boost::dynamic_pointer_cast<ARDOUR::AutomationControl>(
				_route->panner()->data().control(Evoral::Parameter(PanAutomation)));

	if (pan_control) {
		pan_control->alist()->automation_state_changed.connect (panstate_connection, boost::bind (&PannerUI::pan_automation_state_changed, &panners), gui_context());
		pan_control->alist()->automation_style_changed.connect (panstyle_connection, boost::bind (&PannerUI::pan_automation_style_changed, &panners), gui_context());
	}

	panners.pan_changed (this);
}


/*
 * Output port labelling
 * =====================
 *
 * Case 1: Each output has one connection, all connections are to system:playback_%i
 *   out 1 -> system:playback_1
 *   out 2 -> system:playback_2
 *   out 3 -> system:playback_3
 *   Display as: 1/2/3
 *
 * Case 2: Each output has one connection, all connections are to ardour:track_x/in 1
 *   out 1 -> ardour:track_x/in 1
 *   out 2 -> ardour:track_x/in 2
 *   Display as: track_x
 *
 * Case 3: Each output has one connection, all connections are to Jack client "program x"
 *   out 1 -> program x:foo
 *   out 2 -> program x:foo
 *   Display as: program x
 *
 * Case 4: No connections (Disconnected)
 *   Display as: -
 *
 * Default case (unusual routing):
 *   Display as: *number of connections*
 *
 * Tooltips
 * ========
 * .-----------------------------------------------.
 * | Mixdown                                       |
 * | out 1 -> ardour:master/in 1, jamin:input/in 1 |
 * | out 2 -> ardour:master/in 2, jamin:input/in 2 |
 * '-----------------------------------------------'
 * .-----------------------------------------------.
 * | Guitar SM58                                   |
 * | Disconnected                                  |
 * '-----------------------------------------------'
 */

void
MixerStrip::update_io_button (boost::shared_ptr<ARDOUR::Route> route, Width width, bool for_input)
{
	uint32_t io_count;
	uint32_t io_index;
	Port *port;
	vector<string> port_connections;

	uint32_t total_connection_count = 0;
	uint32_t io_connection_count = 0;
	uint32_t ardour_connection_count = 0;
	uint32_t system_connection_count = 0;
	uint32_t other_connection_count = 0;

	ostringstream label;
	string label_string;
	char * label_cstr;

	bool have_label = false;
	bool each_io_has_one_connection = true;

	string connection_name;
	string ardour_track_name;
	string other_connection_type;
	string system_ports;
	string system_port;

	ostringstream tooltip;
	char * tooltip_cstr;

	tooltip << route->name();

	if (for_input) {
		io_count = route->n_inputs().n_total();
	} else {
		io_count = route->n_outputs().n_total();
	}

	for (io_index = 0; io_index < io_count; ++io_index) {
		if (for_input) {
			port = route->input()->nth (io_index);
		} else {
			port = route->output()->nth (io_index);
		}
		
		port_connections.clear ();
		port->get_connections(port_connections);
		io_connection_count = 0;

		if (!port_connections.empty()) {
			for (vector<string>::iterator i = port_connections.begin(); i != port_connections.end(); ++i) {
				string& connection_name (*i);

				if (io_connection_count == 0) {
					tooltip << endl << port->name().substr(port->name().find("/") + 1) << " -> " << connection_name;
				} else {
					tooltip << ", " << connection_name;
				}

				if (connection_name.find("ardour:") == 0) {
					if (ardour_track_name.empty()) {
						// "ardour:Master/in 1" -> "ardour:Master/"
						string::size_type slash = connection_name.find("/");
						if (slash != string::npos) {
							ardour_track_name = connection_name.substr(0, slash + 1);
						}
					}

					if (connection_name.find(ardour_track_name) == 0) {
						++ardour_connection_count;
					}
				} else if (connection_name.find("system:") == 0) {
					if (for_input) {
						// "system:capture_123" -> "123"
						system_port = connection_name.substr(15);
					} else {
						// "system:playback_123" -> "123"
						system_port = connection_name.substr(16);
					}

					if (system_ports.empty()) {
						system_ports += system_port;
					} else {
						system_ports += "/" + system_port;
					}

					++system_connection_count;
				} else {
					if (other_connection_type.empty()) {
						// "jamin:in 1" -> "jamin:"
						other_connection_type = connection_name.substr(0, connection_name.find(":") + 1);
					}

					if (connection_name.find(other_connection_type) == 0) {
						++other_connection_count;
					}
				}

				++total_connection_count;
				++io_connection_count;
			}
		}

		if (io_connection_count != 1) {
			each_io_has_one_connection = false;
		}
	}

	if (total_connection_count == 0) {
		tooltip << endl << _("Disconnected");
	}

	tooltip_cstr = new char[tooltip.str().size() + 1];
	strcpy(tooltip_cstr, tooltip.str().c_str());

	if (for_input) {
		ARDOUR_UI::instance()->set_tip (&input_button, tooltip_cstr, "");
  	} else {
		ARDOUR_UI::instance()->set_tip (&output_button, tooltip_cstr, "");
	}

	if (each_io_has_one_connection) {
		if ((total_connection_count == ardour_connection_count)) {
			// all connections are to the same track in ardour
			// "ardour:Master/" -> "Master"
			string::size_type slash = ardour_track_name.find("/");
			if (slash != string::npos) {
				label << ardour_track_name.substr(7, slash - 7);
				have_label = true;
			}
		}
		else if (total_connection_count == system_connection_count) {
			// all connections are to system ports
			label << system_ports;
			have_label = true;
		}
		else if (total_connection_count == other_connection_count) {
			// all connections are to the same external program eg jamin
			// "jamin:" -> "jamin"
			label << other_connection_type.substr(0, other_connection_type.size() - 1);
			have_label = true;
		}
	}

	if (!have_label) {
		if (total_connection_count == 0) {
			// Disconnected
			label << "-";
		} else {
			// Odd configuration
			label << "*" << total_connection_count << "*";
		}
	}

	switch (width) {
	case Wide:
		label_string = label.str().substr(0, 6);
		break;
	case Narrow:
		label_string = label.str().substr(0, 3);
		break;
  	}

	label_cstr = new char[label_string.size() + 1];
	strcpy(label_cstr, label_string.c_str());

	if (for_input) {
		input_label.set_text (label_cstr);
	} else {
		output_label.set_text (label_cstr);
	}
}

void
MixerStrip::update_input_display ()
{
	update_io_button (_route, _width, true);
  	panners.setup_pan ();
}

void
MixerStrip::update_output_display ()
{
	update_io_button (_route, _width, false);
  	gpm.setup_meters ();
  	panners.setup_pan ();
}

void
MixerStrip::fast_update ()
{
	gpm.update_meters ();
}

void
MixerStrip::diskstream_changed ()
{
	Gtkmm2ext::UI::instance()->call_slot (boost::bind (&MixerStrip::update_diskstream_display, this));
}

void
MixerStrip::input_changed (IOChange /*change*/, void */*src*/)
{
	Gtkmm2ext::UI::instance()->call_slot (boost::bind (&MixerStrip::update_input_display, this));
	set_width_enum (_width, this);
}

void
MixerStrip::output_changed (IOChange /*change*/, void */*src*/)
{
	Gtkmm2ext::UI::instance()->call_slot (boost::bind (&MixerStrip::update_output_display, this));
	set_width_enum (_width, this);
}


void
MixerStrip::comment_editor_done_editing()
{
	string str =  comment_area->get_buffer()->get_text();
	if (_route->comment() != str) {
		_route->set_comment (str, this);

		switch (_width) {

		case Wide:
			if (! str.empty()) {
			        comment_button.modify_bg (STATE_NORMAL, color());
				((Gtk::Label*)comment_button.get_child())->set_text (_("*Comments*"));
			} else {
			        comment_button.unset_bg (STATE_NORMAL);
				((Gtk::Label*)comment_button.get_child())->set_text (_("Comments"));
			}
			break;

		case Narrow:
			if (! str.empty()) {
			        comment_button.modify_bg (STATE_NORMAL, color());
				((Gtk::Label*)comment_button.get_child())->set_text (_("*Cmt*"));
			} else {
			        comment_button.unset_bg (STATE_NORMAL);
				((Gtk::Label*)comment_button.get_child())->set_text (_("Cmt"));
			}
			break;
		}

		ARDOUR_UI::instance()->tooltips().set_tip (comment_button,
				str.empty() ? _("Click to Add/Edit Comments") : str);
	}

}

void
MixerStrip::comment_button_clicked ()
{
	if (comment_window == 0) {
		setup_comment_editor ();
	}

    int x, y, cw_width, cw_height;

	if (comment_window->is_visible()) {
		comment_window->hide ();
		return;
	}

	comment_window->get_size (cw_width, cw_height);
	comment_window->get_position(x, y);
	comment_window->move(x, y - (cw_height / 2) - 45);
	/*
	   half the dialog height minus the comments button height
	   with some window decoration fudge thrown in.
	*/

	comment_window->show();
	comment_window->present();
}

void
MixerStrip::setup_comment_editor ()
{
	string title;
	title = _route->name();
	title += _(": comment editor");

	comment_window = new ArdourDialog (title, false);
	comment_window->set_position (Gtk::WIN_POS_MOUSE);
	comment_window->set_skip_taskbar_hint (true);
	comment_window->signal_hide().connect (sigc::mem_fun(*this, &MixerStrip::comment_editor_done_editing));

	comment_area = manage (new TextView());
	comment_area->set_name ("MixerTrackCommentArea");
	comment_area->set_size_request (110, 178);
	comment_area->set_wrap_mode (WRAP_WORD);
	comment_area->set_editable (true);
	comment_area->get_buffer()->set_text (_route->comment());
	comment_area->show ();

	comment_window->get_vbox()->pack_start (*comment_area);
	comment_window->get_action_area()->hide();
}

void
MixerStrip::comment_changed (void *src)
{
	ENSURE_GUI_THREAD (*this, &MixerStrip::comment_changed, src)

	if (src != this) {
		ignore_comment_edit = true;
		if (comment_area) {
			comment_area->get_buffer()->set_text (_route->comment());
		}
		ignore_comment_edit = false;
	}
}

void
MixerStrip::set_route_group (RouteGroup *rg)
{
	rg->add (_route);
}

bool
MixerStrip::select_route_group (GdkEventButton *ev)
{
	using namespace Menu_Helpers;

	if (ev->button == 1) {

		if (group_menu == 0) {

			group_menu = new RouteGroupMenu (
				_session,
				(RouteGroup::Property) (RouteGroup::Gain | RouteGroup::Mute | RouteGroup::Solo)
				);

			group_menu->GroupSelected.connect (sigc::mem_fun (*this, &MixerStrip::set_route_group));
		}

		group_menu->popup (1, ev->time);
	}

	return true;
}

void
MixerStrip::route_group_changed ()
{
	ENSURE_GUI_THREAD (*this, &MixerStrip::route_group_changed)

	RouteGroup *rg = _route->route_group();

	if (rg) {
		/* XXX: this needs a better algorithm */
		string truncated = rg->name ();
		if (truncated.length () > 5) {
			truncated = truncated.substr (0, 5);
		}
		group_label.set_text (truncated);
	} else {
		switch (_width) {
		case Wide:
			group_label.set_text (_("Grp"));
			break;
		case Narrow:
			group_label.set_text (_("~G"));
			break;
		}
	}
}


void
MixerStrip::route_gui_changed (string what_changed, void* ignored)
{
	ENSURE_GUI_THREAD (*this, &MixerStrip::route_gui_changed, what_changed, ignored)

	if (what_changed == "color") {
		if (set_color_from_route () == 0) {
			show_route_color ();
		}
	}
}

void
MixerStrip::show_route_color ()
{
	name_button.modify_bg (STATE_NORMAL, color());
	top_event_box.modify_bg (STATE_NORMAL, color());
	route_active_changed ();
}

void
MixerStrip::show_passthru_color ()
{
	route_active_changed ();
}

void
MixerStrip::build_route_ops_menu ()
{
	using namespace Menu_Helpers;
	route_ops_menu = new Menu;
	route_ops_menu->set_name ("ArdourContextMenu");

	MenuList& items = route_ops_menu->items();

	items.push_back (MenuElem (_("Save As Template"), sigc::mem_fun(*this, &RouteUI::save_as_template)));
	items.push_back (MenuElem (_("Rename"), sigc::mem_fun(*this, &RouteUI::route_rename)));
	rename_menu_item = &items.back();
	items.push_back (SeparatorElem());
	items.push_back (CheckMenuElem (_("Active"), sigc::mem_fun (*this, &RouteUI::toggle_route_active)));
	route_active_menu_item = dynamic_cast<CheckMenuItem *> (&items.back());
	route_active_menu_item->set_active (_route->active());

	items.push_back (SeparatorElem());

	items.push_back (MenuElem (_("Adjust latency"), sigc::mem_fun (*this, &RouteUI::adjust_latency)));

	items.push_back (SeparatorElem());
	items.push_back (CheckMenuElem (_("Invert Polarity"), sigc::mem_fun (*this, &RouteUI::toggle_polarity)));
	polarity_menu_item = dynamic_cast<CheckMenuItem *> (&items.back());
	polarity_menu_item->set_active (_route->phase_invert());
	items.push_back (CheckMenuElem (_("Protect against denormals"), sigc::mem_fun (*this, &RouteUI::toggle_denormal_protection)));
	denormal_menu_item = dynamic_cast<CheckMenuItem *> (&items.back());
	denormal_menu_item->set_active (_route->denormal_protection());

	if (!Profile->get_sae()) {
		items.push_back (SeparatorElem());
		items.push_back (MenuElem (_("Remote Control ID..."), sigc::mem_fun (*this, &RouteUI::open_remote_control_id_dialog)));
        }

	items.push_back (SeparatorElem());
	items.push_back (MenuElem (_("Remove"), sigc::mem_fun(*this, &RouteUI::remove_this_route)));
}

gint
MixerStrip::name_button_button_press (GdkEventButton* ev)
{
	if (ev->button == 1 || ev->button == 3) {
		list_route_operations ();

		/* do not allow rename if the track is record-enabled */
		rename_menu_item->set_sensitive (!_route->record_enabled());
		route_ops_menu->popup (1, ev->time);
	}
	return FALSE;
}

void
MixerStrip::list_route_operations ()
{
	if (route_ops_menu == 0) {
		build_route_ops_menu ();
	}
}

void
MixerStrip::set_selected (bool yn)
{
	AxisView::set_selected (yn);
	if (_selected) {
		global_frame.set_shadow_type (Gtk::SHADOW_ETCHED_OUT);
		global_frame.set_name ("MixerStripSelectedFrame");
	} else {
		global_frame.set_shadow_type (Gtk::SHADOW_IN);
		global_frame.set_name ("MixerStripFrame");
	}
	global_frame.queue_draw ();
}

void
MixerStrip::name_changed ()
{
	switch (_width) {
	case Wide:
		RouteUI::name_changed ();
		break;
	case Narrow:
	        name_label.set_text (PBD::short_version (_route->name(), 5));
		break;
	}
	if (_route->phase_invert()) {
	        name_label.set_text (X_("Ø ") + name_label.get_text());
	}
}

void
MixerStrip::width_clicked ()
{
	switch (_width) {
	case Wide:
		set_width_enum (Narrow, this);
		break;
	case Narrow:
		set_width_enum (Wide, this);
		break;
	}
}

void
MixerStrip::hide_clicked ()
{
	// LAME fix to reset the button status for when it is redisplayed (part 1)
	hide_button.set_sensitive(false);

	if (_embedded) {
		Hiding(); /* EMIT_SIGNAL */
	} else {
		_mixer.hide_strip (this);
	}

	// (part 2)
	hide_button.set_sensitive(true);
}

void
MixerStrip::set_embedded (bool yn)
{
	_embedded = yn;
}

void
MixerStrip::map_frozen ()
{
	ENSURE_GUI_THREAD (*this, &MixerStrip::map_frozen)

	boost::shared_ptr<AudioTrack> at = audio_track();

	if (at) {
		switch (at->freeze_state()) {
		case AudioTrack::Frozen:
			processor_box.set_sensitive (false);
			break;
		default:
			processor_box.set_sensitive (true);
			// XXX need some way, maybe, to retoggle redirect editors
			break;
		}
	}

	hide_redirect_editors ();
}

void
MixerStrip::hide_redirect_editors ()
{
	_route->foreach_processor (sigc::mem_fun (*this, &MixerStrip::hide_processor_editor));
}

void
MixerStrip::hide_processor_editor (boost::weak_ptr<Processor> p)
{
	boost::shared_ptr<Processor> processor (p.lock ());
	if (!processor) {
		return;
	}

	void* gui = processor->get_gui ();

	if (gui) {
		static_cast<Gtk::Widget*>(gui)->hide ();
	}
}

void
MixerStrip::route_active_changed ()
{
	RouteUI::route_active_changed ();
	reset_strip_style ();
}

void
MixerStrip::reset_strip_style ()
{
	if (_current_delivery && boost::dynamic_pointer_cast<Send>(_current_delivery)) {

		gpm.set_fader_name ("SendStripBase");

	} else {
		
		if (is_midi_track()) {
			if (_route->active()) {
				set_name ("MidiTrackStripBase");
				gpm.set_meter_strip_name ("MidiTrackStripBase");
			} else {
				set_name ("MidiTrackStripBaseInactive");
				gpm.set_meter_strip_name ("MidiTrackStripBaseInactive");
			}
			gpm.set_fader_name ("MidiTrackFader");
		} else if (is_audio_track()) {
			if (_route->active()) {
				set_name ("AudioTrackStripBase");
				gpm.set_meter_strip_name ("AudioTrackMetrics");
			} else {
				set_name ("AudioTrackStripBaseInactive");
				gpm.set_meter_strip_name ("AudioTrackMetricsInactive");
			}
			gpm.set_fader_name ("AudioTrackFader");
		} else {
			if (_route->active()) {
				set_name ("AudioBusStripBase");
				gpm.set_meter_strip_name ("AudioBusMetrics");
			} else {
				set_name ("AudioBusStripBaseInactive");
				gpm.set_meter_strip_name ("AudioBusMetricsInactive");
			}
			gpm.set_fader_name ("AudioBusFader");
			
			/* (no MIDI busses yet) */
		}
	}
}

RouteGroup*
MixerStrip::route_group() const
{
	return _route->route_group();
}

void
MixerStrip::engine_stopped ()
{
}

void
MixerStrip::engine_running ()
{
}

/** Called when the metering point has changed */
void
MixerStrip::meter_changed (void *src)
{
	ENSURE_GUI_THREAD (*this, &MixerStrip::meter_changed, src)

	switch (_route->meter_point()) {
	case MeterInput:
		meter_point_label.set_text (_("input"));
		break;

	case MeterPreFader:
		meter_point_label.set_text (_("pre"));
		break;

	case MeterPostFader:
		meter_point_label.set_text (_("post"));
		break;

	case MeterCustom:
		meter_point_label.set_text (_("custom"));
		break;
	}

	gpm.setup_meters ();
	// reset peak when meter point changes
	gpm.reset_peak_display();
}

void
MixerStrip::switch_io (boost::shared_ptr<Route> target)
{
	/* don't respond to switch IO signal outside of the mixer window */

	if (!_mixer_owned) {
		return;
	}

	if (_route == target || _route->is_master()) {
		/* don't change the display for the target or the master bus */
		return;
	} else if (!is_track() && show_sends_button) {
		/* make sure our show sends button is inactive, and we no longer blink,
		   since we're not the target.
		*/
		send_blink_connection.disconnect ();
		show_sends_button->set_active (false);
		show_sends_button->set_state (STATE_NORMAL);
	}

	if (!target) {
		/* switch back to default */
		revert_to_default_display ();
		return;
	}

	boost::shared_ptr<Send> send = _route->internal_send_for (target);

	if (send) {
		show_send (send);
	} else {
		revert_to_default_display ();
	}
}

void
MixerStrip::drop_send ()
{
	boost::shared_ptr<Send> current_send;

	if (_current_delivery && (current_send = boost::dynamic_pointer_cast<Send>(_current_delivery))) {
		current_send->set_metering (false);
	}

	send_gone_connection.disconnect ();
	input_button.set_sensitive (true);
	output_button.set_sensitive (true);
}

void
MixerStrip::show_send (boost::shared_ptr<Send> send)
{
	assert (send != 0);

	drop_send ();

	_current_delivery = send;

	send->set_metering (true);
	_current_delivery->DropReferences.connect (send_gone_connection, boost::bind (&MixerStrip::revert_to_default_display, this), gui_context());

	gain_meter().set_controls (_route, send->meter(), send->amp());
	gain_meter().setup_meters ();

	panner_ui().set_panner (_current_delivery->panner());
	panner_ui().setup_pan ();

	input_button.set_sensitive (false);

	if (boost::dynamic_pointer_cast<InternalSend>(send)) {
		output_button.set_sensitive (false);
	}

	reset_strip_style ();
}

void
MixerStrip::revert_to_default_display ()
{
	if (show_sends_button) {
		show_sends_button->set_active (false);
	}

	drop_send ();

	_current_delivery = _route->main_outs();

	gain_meter().set_controls (_route, _route->shared_peak_meter(), _route->amp());
	gain_meter().setup_meters ();

	panner_ui().set_panner (_route->main_outs()->panner());
	panner_ui().setup_pan ();

	reset_strip_style ();
}

void
MixerStrip::set_button_names ()
{
	switch (_width) {
	case Wide:
		rec_enable_button_label.set_text (_("Rec"));
		mute_button_label.set_text (_("Mute"));
		if (!Config->get_solo_control_is_listen_control()) {
			solo_button_label.set_text (_("Solo"));
		} else {
			switch (Config->get_listen_position()) {
			case AfterFaderListen:
				solo_button_label.set_text (_("AFL"));
				break;
			case PreFaderListen:
				solo_button_label.set_text (_("PFL"));
				break;
			}
		}
		break;

	default:
		rec_enable_button_label.set_text (_("R"));
		mute_button_label.set_text (_("M"));
		if (!Config->get_solo_control_is_listen_control()) {
			solo_button_label.set_text (_("S"));
		} else {
			switch (Config->get_listen_position()) {
			case AfterFaderListen:
				solo_button_label.set_text (_("A"));
				break;
			case PreFaderListen:
				solo_button_label.set_text (_("P"));
				break;
			}
		}
		break;

	}
}

bool
MixerStrip::on_key_press_event (GdkEventKey* ev)
{
	GdkEventButton fake;
	fake.type = GDK_BUTTON_PRESS;
	fake.button = 1;
	fake.state = ev->state;

	switch (ev->keyval) {
	case GDK_m:
		mute_press (&fake);
		return true;
		break;

	case GDK_s:
		solo_press (&fake);
		return true;
		break;

	case GDK_r:
		rec_enable_press (&fake);
		return true;
		break;

	case GDK_e:
		show_sends_press (&fake);
		return true;
		break;

	case GDK_g:
		if (ev->state & Keyboard::PrimaryModifier) {
			step_gain_down ();
		} else {
			step_gain_up ();
		}
		return true;
		break;

	case GDK_0:
		if (_route) {
			_route->set_gain (1.0, this);
		}
		return true;

	default:
		break;
	}

	return false;
}


bool
MixerStrip::on_key_release_event (GdkEventKey* ev)
{
	GdkEventButton fake;
	fake.type = GDK_BUTTON_RELEASE;
	fake.button = 1;
	fake.state = ev->state;

	switch (ev->keyval) {
	case GDK_m:
		mute_release (&fake);
		return true;
		break;

	case GDK_s:
		solo_release (&fake);
		return true;
		break;

	case GDK_r:
		rec_enable_release (&fake);
		return true;
		break;

	case GDK_e:
		show_sends_release (&fake);
		return true;
		break;

	case GDK_g:
		return true;
		break;

	default:
		break;
	}

	return false;
}

bool
MixerStrip::on_enter_notify_event (GdkEventCrossing*)
{
	Keyboard::magic_widget_grab_focus ();
	grab_focus ();
	return false;
}

bool
MixerStrip::on_leave_notify_event (GdkEventCrossing* ev)
{
	switch (ev->detail) {
	case GDK_NOTIFY_INFERIOR:
		break;
	default:
		Keyboard::magic_widget_drop_focus ();
	}

	return false;
}

void
MixerStrip::mono_button_clicked ()
{
	panners.set_mono (_mono_button.get_active ());
}

PluginSelector*
MixerStrip::plugin_selector()
{
	return _mixer.plugin_selector();
}
