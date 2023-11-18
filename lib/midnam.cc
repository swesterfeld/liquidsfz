// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "pugixml.hh"
#include "liquidsfz.hh"
#include "midnam.hh"

using pugi::xml_node;
using pugi::xml_document;
using std::string;

namespace LiquidSFZInternal
{

string
gen_midnam (const LiquidSFZ::Synth& synth, const string& model)
{
  xml_document doc;

  doc.append_child (pugi::node_doctype).set_value ("MIDINameDocument PUBLIC"
                                                   " \"-//MIDI Manufacturers Association//DTD MIDINameDocument 1.0//EN\""
                                                   " \"http://www.midi.org/dtds/MIDINameDocument10.dtd\"");

  xml_node midi_name_document = doc.append_child ("MIDINameDocument");

  midi_name_document.append_child ("Author");
  xml_node master_device_names = midi_name_document.append_child ("MasterDeviceNames");
  master_device_names.append_child ("Manufacturer").append_child (pugi::node_pcdata).set_value ("LiquidSFZ");
  master_device_names.append_child ("Model").append_child (pugi::node_pcdata).set_value (model.c_str());

  // ------------ <CustomDeviceMode>
  xml_node custom_device_mode = master_device_names.append_child ("CustomDeviceMode");
  custom_device_mode.append_attribute ("Name").set_value ("Default");

  xml_node channel_name_set_assignments = custom_device_mode.append_child ("ChannelNameSetAssignments");
  for (int channel = 1; channel <= 16; channel++)
    {
      pugi::xml_node channel_name_set_assign = channel_name_set_assignments.append_child ("ChannelNameSetAssign");
      channel_name_set_assign.append_attribute ("Channel").set_value (channel);
      channel_name_set_assign.append_attribute ("NameSet").set_value ("Names");
    }
  // ------------ </CustomDeviceMode>

  // ------------ <ChannelNameSet>
  xml_node channel_name_set = master_device_names.append_child ("ChannelNameSet");
  channel_name_set.append_attribute ("Name").set_value("Names");

  xml_node available_for_channels = channel_name_set.append_child ("AvailableForChannels");
  for (int channel = 1; channel <= 16; channel++)
    {
      xml_node available_channel = available_for_channels.append_child ("AvailableChannel");
      available_channel.append_attribute ("Channel").set_value (channel);
      available_channel.append_attribute ("Available").set_value ("true");
    }
  channel_name_set.append_child ("UsesControlNameList").append_attribute ("Name").set_value ("Controls");
  channel_name_set.append_child ("UsesNoteNameList").append_attribute ("Name").set_value ("Notes");
  // ------------ </ChannelNameSet>

  // ------------ <NoteNameList>
  xml_node note_name_list = master_device_names.append_child ("NoteNameList");
  note_name_list.append_attribute ("Name").set_value ("Notes");
  for (const auto& key_info : synth.list_keys())
    {
      if (key_info.label() != "")
        {
          xml_node note = note_name_list.append_child ("Note");
          note.append_attribute("Number").set_value (key_info.key());
          note.append_attribute("Name").set_value (key_info.label().c_str());
        }
    }
  // </NoteNameList>

  // ------------- <ControlNameList>
  pugi::xml_node control_name_list = master_device_names.append_child ("ControlNameList");
  control_name_list.append_attribute ("Name").set_value ("Controls");
  for (const auto& cc_info : synth.list_ccs())
    {
      xml_node control = control_name_list.append_child ("Control");
      control.append_attribute ("Type").set_value ("7bit");
      control.append_attribute ("Number").set_value (cc_info.cc());
      control.append_attribute ("Name").set_value (cc_info.label().c_str());
    }
  // ------------- </ControlNameList>

  struct xml_string_writer : pugi::xml_writer
  {
    string result;

    virtual void
    write (const void* data, size_t size)
    {
      result.append (static_cast<const char*>(data), size);
    }
  } writer;

  doc.save (writer);
  return writer.result;
}

}
