// RTF grammar, attributed GTK signals and file transactions. MIT.
#include "gtk-richtext.hpp"
#include <iostream>
#include <fstream>

using namespace vb6;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void error(int number,const std::function<void()>& action) {
  try{action();}catch(const Error& e){if(e.number!=number)throw std::runtime_error("Expected VB error "+std::to_string(number)+", got "+std::to_string(e.number)+": "+e.what());return;}
  throw std::runtime_error("Expected VB error "+std::to_string(number));
}
const GtkRtfStyle& at(const GtkRtfDocument& document,size_t offset) {
  for(const auto& run:document.runs)if(run.first<=offset&&offset<run.last)return run.style;
  return document.emptyStyle;
}
void same(const GtkRtfDocument& a,const GtkRtfDocument& b) {
  if(a.text!=b.text)throw std::runtime_error("RTF roundtrip changed Unicode text: ["+toUTF8(a.text)+"] -> ["+toUTF8(b.text)+"]");
  for(size_t i=0;i<a.text.size();++i)if(at(a,i)!=at(b,i))throw std::runtime_error("RTF roundtrip changed attributes at UTF-16 offset "+std::to_string(i));
  require(a.emptyStyle==b.emptyStyle,"RTF roundtrip changed trailing insertion style");
  require(a.fields==b.fields,"RTF roundtrip changed inert field instructions or ranges");
  require(a.objects.size()==b.objects.size(),"RTF roundtrip dropped embedded objects");
  if(!a.objects.empty())require(gtkRtfWrite(a)==gtkRtfWrite(b),"RTF roundtrip changed structured object content");
}
const std::string redPixel="89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c63f8cfc0f01f00050001ff89993d1d0000000049454e44ae426082";
std::string hexBytes(const std::string& bytes){static const char digits[]="0123456789abcdef";std::string value;for(auto ch:bytes){value+=digits[uint8_t(ch)>>4];value+=digits[uint8_t(ch)&15];}return value;}
GtkWidget* anchoredWidget(GtkTextChildAnchor* anchor) {
#if GTK_MAJOR_VERSION < 4
  auto* widgets=gtk_text_child_anchor_get_widgets(anchor);auto* result=widgets?GTK_WIDGET(widgets->data):nullptr;g_list_free(widgets);return result;
#else
  guint count=0;auto** widgets=gtk_text_child_anchor_get_widgets(anchor,&count);auto* result=count?widgets[0]:nullptr;g_free(widgets);return result;
#endif
}
GtkWidget* richChild(GtkWidget* widget) {
  if(GTK_IS_TEXT_VIEW(widget))return widget;
#if GTK_MAJOR_VERSION < 4
  if(!GTK_IS_CONTAINER(widget))return nullptr;auto* children=gtk_container_get_children(GTK_CONTAINER(widget));GtkWidget* found=nullptr;for(auto* item=children;item&&!found;item=item->next)found=richChild(GTK_WIDGET(item->data));g_list_free(children);return found;
#else
  for(auto* child=gtk_widget_get_first_child(widget);child;child=gtk_widget_get_next_sibling(child))if(auto* found=richChild(child))return found;return nullptr;
#endif
}
void parser() {
  const std::string sample=R"({\rtf1\ansi\ansicpg1252\deff0{\fonttbl{\f0\fswiss Arial;}{\f1\fmodern Courier New;}}{\colortbl;\red255\green0\blue0;\red0\green255\blue0;}\pard\f0\fs20 plain {\b\i\cf1\highlight2 BOLD} tail\par\qc\li720\fi-360\ri180\sb120\sa240\tx720\tx1440\f1\fs25\uldb Center\tab two\line soft\par\pard\protect\v hidden\v0\protect0\up6 raised\up0\super super\nosupersub})";
  auto doc=gtkRtfParse(sample);require(doc.text==u"plain BOLD tail\nCenter\ttwo\u2028soft\nhiddenraisedsuper","RTF destinations leaked into body text");
  require(at(doc,6).bold&&at(doc,6).italic&&at(doc,6).foreground==255&&at(doc,6).background==65280,"RTF character formatting was lost");
  require(!at(doc,10).bold&&at(doc,10).foreground==-1,"RTF group state did not restore");
  require(at(doc,16).paragraph.alignment==2&&at(doc,16).paragraph.left==720&&at(doc,16).paragraph.first==-360,"RTF paragraph formatting was lost");
  same(doc,gtkRtfParse(gtkRtfWrite(doc)));
  auto unicode=gtkRtfParse(R"({\rtf1\ansi\ansicpg1252 caf\'e9 \uc1\u28450?\u23383? {\uc2\u945ab}\u946? \u-10179?\u-8704?\~\_\\\{\}})");
  require(unicode.text==u"caf\u00e9 \u6f22\u5b57 \u03b1\u03b2 \U0001f600\u00a0\u2011\\{}","RTF Unicode/fallback/group decoding is wrong");
  same(unicode,gtkRtfParse(gtkRtfWrite(unicode)));
  auto dbcs=gtkRtfParse(R"({\rtf1\ansi\ansicpg1252\deff0{\fonttbl{\f0\fcharset0 Arial;}{\f1\fcharset128 MS Gothic;}{\f2\fcharset204 Arial;}}A\f1\'82\'a0\'82\'a2\f2\'cf\'f0\'e8\'e2\'e5\'f2\f0Z})");
  require(dbcs.text==u"A\u3042\u3044\u041f\u0440\u0438\u0432\u0435\u0442Z","RTF font-specific DBCS/Cyrillic decoding is wrong");
  same(dbcs,gtkRtfParse(gtkRtfWrite(dbcs)));
  auto styles=gtkRtfParse(R"({\rtf1\deff0{\fonttbl{\f0 Arial;}}{\stylesheet{\s0\fs20 Normal;}{\s2\sbasedon1\i Derived;}{\s1\sbasedon0\b\qc Heading;}{\*\cs3\additive\ul Emphasis;}}\s2 Heading {\cs3 emphasis}\plain\pard plain})");
  require(at(styles,0).bold&&at(styles,0).italic&&at(styles,0).paragraph.alignment==0,"RTF paragraph reset must affect the complete paragraph");
  require(at(styles,8).bold&&at(styles,8).italic&&at(styles,8).underline==1,"RTF inherited/additive stylesheet formatting was lost");
  same(styles,gtkRtfParse(gtkRtfWrite(styles)));
  auto alternatives=gtkRtfParse(R"({\rtf1{\upr{ANSI}{\*\ud Unicode\u945?}}})");
  require(alternatives.text==u"Unicode\u03b1","RTF Unicode alternative destination was duplicated");
  auto bullet=gtkRtfParse(R"({\rtf1{\pntext\u8226?\tab}{\*\pn\pnlvlblt{\pntxtb\u8226?}}\li720\fi-360 item\par})");
  require(bullet.text==u"item\n"&&at(bullet,0).paragraph.bullet,"RTF bullet list fallback leaked into text");same(bullet,gtkRtfParse(gtkRtfWrite(bullet)));
  auto numbers=gtkRtfParse(R"({\rtf1{\*\pn\pnlvlbody\pnucrm\pnstart4{\pntxtb (}{\pntxta )}}first\par second})");
  require(at(numbers,0).paragraph.numbering==2&&gtkRtfListMarker(at(numbers,0).paragraph,4)==u"(IV)","Numbered paragraphs lost their numbering format or affixes");same(numbers,gtkRtfParse(gtkRtfWrite(numbers)));
  auto custom=gtkRtfParse(R"({\rtf1{\listtext 2.3.\tab}section})");require(custom.text==u"section"&&at(custom,0).paragraph.marker==u"2.3.","Explicit multilevel list marker was discarded");same(custom,gtkRtfParse(gtkRtfWrite(custom)));
  auto spacing=gtkRtfParse(R"({\rtf1\deftab360\sl480\slmult1\tqr\tx720\tqc\tx1440\tqdec\tx2160 first\tab second\tab 12.34\par last})");
  require(at(spacing,0).paragraph.tabAlignments==std::vector<int>({1,2,3})&&at(spacing,0).paragraph.lineSpacing==480&&at(spacing,0).paragraph.lineMultiple==1&&at(spacing,0).paragraph.defaultTab==360,"RTF aligned tabs or multiple line spacing were discarded");same(spacing,gtkRtfParse(gtkRtfWrite(spacing)));
  auto fields=gtkRtfParse(R"({\rtf1 before {\field\fldlock{\*\fldinst HYPERLINK "https://example.org"}{\fldrslt\b Example}} after {\field{\*\fldinst LINK external-file}{\fldrslt cached}}})");
  require(fields.text==u"before Example after cached"&&fields.fields.size()==2&&fields.fields[0].first==7&&fields.fields[0].last==14&&fields.fields[0].locked&&fields.fields[1].instruction==u"LINK external-file","Inert field results or instructions were lost");same(fields,gtkRtfParse(gtkRtfWrite(fields)));
  auto nestedFields=gtkRtfParse(R"({\rtf1{\field{\*\fldinst IF 1}{\fldrslt a{\field{\*\fldinst PAGE}{\fldrslt 2}}z}}})");same(nestedFields,gtkRtfParse(gtkRtfWrite(nestedFields)));
  error(380,[&]{gtkRtfParse(R"({\rtf1{\field{\*\fldinst PAGE}}})");});
  auto picture=gtkRtfParse("{\\rtf1 before{\\pict\\pngblip\\picwgoal300\\pichgoal300 "+redPixel+"}after}");require(picture.text==u"before\ufffcafter"&&picture.objects.size()==1&&picture.objects[0].width==1&&picture.objects[0].widthGoal==300,"RTF picture bytes, geometry or object anchor were lost");same(picture,gtkRtfParse(gtkRtfWrite(picture)));
  const auto binaryPicture="{\\rtf1{\\pict\\pngblip\\bin"+std::to_string(picture.objects[0].bytes.size())+" "+picture.objects[0].bytes+"}}";auto binaryImage=gtkRtfParse(binaryPicture);require(binaryImage.objects[0].bytes==picture.objects[0].bytes,"Binary RTF image data was not preserved");same(binaryImage,gtkRtfParse(gtkRtfWrite(binaryImage)));
  auto table=gtkRtfParse(R"({\rtf1 Before{\trowd\trleft0\trgaph60\clbrdrt\brdrs\brdrw30\cellx1440\clvertalc\cellx2880\intbl\b Left\cell\b0 Right\cell\row}After})");
  require(table.text==u"Before\ufffc\u2028After"&&table.objects.size()==1&&table.objects[0].cells.size()==2&&table.objects[0].cells[0].content->text==u"Left"&&table.objects[0].cells[1].content->text==u"Right"&&table.objects[0].cells[0].borders[0].width==30,"RTF table structure, cell text or borders were lost");same(table,gtkRtfParse(gtkRtfWrite(table)));
  auto nestedTable=gtkRtfParse(R"({\rtf1{\trowd\cellx2880 outer{\trowd\cellx1440 inner\nestcell\nestrow}\cell\row}})");same(nestedTable,gtkRtfParse(gtkRtfWrite(nestedTable)));
  auto regions=gtkRtfParse(R"({\rtf1{\header\b Header}body{\footnote Note}{\footer Footer}{\object\objemb{\*\objclass Word.Document}{\*\objdata 00010203}{\result\i Cached preview}}})");
  require(regions.objects.size()==4&&regions.objects[0].content->text==u"Header"&&regions.objects[1].content->text==u"Note"&&regions.objects[3].bytes==std::string("\0\1\2\3",4)&&regions.objects[3].content->text==u"Cached preview","Page regions or inert OLE result data were lost");same(regions,gtkRtfParse(gtkRtfWrite(regions)));
  for(const auto& input:{"{\\rtf1{\\pict\\pngblip deadbeef}}","{\\rtf1\\trowd\\cell text}","{\\rtf1\\trowd\\cellx1440 text\\row}","{\\rtf1\\trowd\\clmrg\\cellx1440\\cell\\row}"})error(380,[&]{gtkRtfParse(input);});
  auto symbol=gtkRtfParse(R"({\rtf1{\fonttbl{\f0\fcharset2 Symbol;}{\f1\fcharset42 Custom;}}\f0\u65?\u945?\f1\u66?})");
  require(symbol.text==u"A\u03b1B"&&at(symbol,0).charset==2&&at(symbol,2).charset==42,"Explicit Unicode with legacy font charset was lost");same(symbol,gtkRtfParse(gtkRtfWrite(symbol)));
  error(453,[&]{gtkRtfParse(R"({\rtf1{\fonttbl{\f0\fcharset2 Symbol;}}\f0 raw})");});
  auto overriddenScript=gtkRtfParse(R"({\rtf1\up6\super superscript\dn3 baseline})");same(overriddenScript,gtkRtfParse(gtkRtfWrite(overriddenScript)));
  auto nativeUnsupported=symbol;nativeUnsupported.runs.front().style.paragraph.lineExtra=15;error(453,[&]{gtkRtfWrite(nativeUnsupported);});
  auto scopedParagraph=gtkRtfParse(R"({\rtf1{\qc centered}})");same(scopedParagraph,gtkRtfParse(gtkRtfWrite(scopedParagraph)));
  auto paragraphSeparators=gtkRtfParse(R"({\rtf1\qc one\u13?\u10?\qr two\u8233?\ql three\u13?})");same(paragraphSeparators,gtkRtfParse(gtkRtfWrite(paragraphSeparators)));
  auto info=gtkRtfParse(R"({\rtf1{\info{\title Example}{\author Test}}{\*\generator VB6;}\paperw12240\paperh15840 text})");
  auto saved=gtkRtfWrite(info);require(saved.find("\\title Example")!=std::string::npos&&saved.find("\\paperw12240")!=std::string::npos,"RTF metadata was not preserved");
  for(const std::string input:{"", "{\\rtf1", "{\\rtf1 ok}}", "{\\rtf1 ok}garbage", "{\\rtf1\\'xy}", "{\\rtf1\\u-10179?}", "{\\rtf1\\bin999 xx}", "{\\rtf1\\cf20 x}", "{\\rtf1\\fs99999999999 x}", "{\\rtf1{\\stylesheet{\\s1\\sbasedon2 A;}{\\s2\\sbasedon1 B;}}\\s1 x}"})error(380,[&]{gtkRtfParse(input);});
  for(const std::string input:{R"({\rtf1{\object\objdata 00}})",R"({\rtf1\ulw text})",R"({\rtf1{\*\unknownFeature ignored}})"})error(453,[&]{gtkRtfParse(input);});
  error(7,[&]{gtkRtfParse("{\\rtf1"+std::string(256,'{')+"x"+std::string(257,'}'));});
  error(7,[&]{gtkRtfParse(std::string(GtkRtfMaxBytes+1,'x'));});
  error(380,[&]{gtkRtfParse(std::string("{\\rtf1 a")+'\0'+"b}");});
  std::cout<<"LINUX_RTF_PARSER_OK\n";
}
ControlSpec spec(const std::string& type,const std::string& name){ControlSpec value;value.type=type;value.name=name;value.properties={{"width",Value::integer(6000)},{"height",Value::integer(3000)}};return value;}
}
int main() {
  try {
    parser();Runtime runtime(makeNativeHost());runtime.name="GTK RichTextBox contracts";auto& host=static_cast<GtkHost&>(*runtime.host);
    Module module;module.name="RichForm";module.kind="form";module.form=spec("Form","RichForm");module.controls.push_back(spec("RichTextBox","Editor"));module.controls.push_back(spec("TextBox","Plain"));
    int changes=0,selections=0;Procedure change;change.name="editor_change";change.code=[&](Frame&){++changes;return Value{};};module.procedures[change.name]=change;
    Procedure selected;selected.name="editor_selchange";selected.code=[&](Frame&){++selections;return Value{};};module.procedures[selected.name]=selected;
    runtime.modules["richform"]=module;auto form=runtime.instance("RichForm");runtime.load(form);
    auto editor=std::dynamic_pointer_cast<GtkControl>(form->controls.at("editor"));auto plain=std::dynamic_pointer_cast<GtkControl>(form->controls.at("plain"));
    require(GTK_IS_TEXT_VIEW(editor->widget),"RichTextBox is not native GtkTextView");
    editor->set(runtime,"text",Value::string(u"plain BOLD tail"));editor->editSelect(6,4);changes=0;
    editor->set(runtime,"selbold",Value::boolean(true));require(changes==1,"Selection formatting must emit one Change");
    editor->set(runtime,"selitalic",Value::boolean(true));editor->set(runtime,"selunderline",Value::boolean(true));editor->set(runtime,"selstrikethru",Value::boolean(true));
    editor->set(runtime,"selfontsize",Value::real(12.5));editor->set(runtime,"selcolor",Value::integer(255));editor->set(runtime,"selbackcolor",Value::integer(65280));
    require(editor->editSelection()==std::make_pair(size_t(6),size_t(4)),"Formatting disturbed UTF-16 selection");
    auto* buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor->widget));GtkTextIter iter;gtk_text_buffer_get_iter_at_offset(buffer,&iter,6);
#if GTK_MAJOR_VERSION < 4
    auto* attributes=gtk_text_attributes_new();gtk_text_iter_get_attributes(&iter,attributes);
    require(pango_font_description_get_weight(attributes->font)==PANGO_WEIGHT_BOLD&&attributes->appearance.underline==PANGO_UNDERLINE_SINGLE,"VB formatting did not reach actual GTK text tags");gtk_text_attributes_unref(attributes);
#else
    auto* actualTags=gtk_text_iter_get_tags(&iter);require(actualTags,"VB formatting did not create native text tags");int weight=0,underline=0;g_object_get(actualTags->data,"weight",&weight,"underline",&underline,nullptr);g_slist_free(actualTags);require(weight==PANGO_WEIGHT_BOLD&&underline==PANGO_UNDERLINE_SINGLE,"VB formatting did not reach actual GTK4 text tags");
#endif
    auto fragment=editor->get(runtime,"selrtf");editor->editSelect(0,10);require(editor->get(runtime,"selbold").type==Type::Null,"Mixed rich selection did not return Null");
    editor->set(runtime,"text",Value::string(u"# tail"));editor->editSelect(0,1);auto parsedFragment=gtkRtfParse(gtkRtfBytes(fragment.string()));require(parsedFragment.text==u"BOLD","Selected fragment parser text differs");editor->set(runtime,"selrtf",fragment);
    require(editor->editText()==u"BOLD tail"&&editor->editSelection()==std::make_pair(size_t(4),size_t(0)),"SelRTF replacement lost text or caret position");
    editor->editSelect(0,4);require(editor->get(runtime,"selbold").truth()&&editor->get(runtime,"selitalic").truth(),"SelRTF did not preserve attributes");
    editor->editSelect(2,0);editor->set(runtime,"selrtf",Value::string(u"{\\rtf1\\qr fragment\\par\\qc next}"));
    auto paste=gtkRichSnapshot(*editor);same(paste,gtkRtfParse(gtkRtfWrite(paste)));
    editor->editSelect(2,1);require(editor->get(runtime,"selalignment").integral()==0,"Fragment paragraph formatting changed an existing paragraph's leading style");
    editor->editSelect(11,1);require(editor->get(runtime,"selalignment").integral()==2,"Inserted new paragraph did not use fragment paragraph formatting");
    editor->set(runtime,"text",Value::string(u"# tail"));editor->editSelect(0,1);editor->set(runtime,"selrtf",fragment);editor->editSelect(0,4);
    auto preserved=editor->get(runtime,"textrtf");auto selectedRange=editor->editSelection();changes=0;
    for(const auto& rtf:{u"bad",u"{\\rtf1 incomplete",u"{\\rtf1{\\pict xx}}"})error(rtf==Text(u"{\\rtf1{\\pict xx}}")?453:380,[&]{editor->set(runtime,"textrtf",Value::string(rtf));});
    error(380,[&]{editor->set(runtime,"selfontsize",Value::integer(0));});error(380,[&]{editor->set(runtime,"selbold",Value::null());});
    require(editor->get(runtime,"textrtf").string()==preserved.string()&&editor->editSelection()==selectedRange&&changes==0,"Failed RTF/formatting update was not transactional");
    error(438,[&]{plain->set(runtime,"selbold",Value::boolean(true));});
    editor->editSelect(4,0);editor->set(runtime,"selbold",Value::boolean(false));editor->set(runtime,"selitalic",Value::boolean(false));editor->set(runtime,"selfontsize",Value::real(14));changes=0;
    buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor->widget));gtk_text_buffer_insert_at_cursor(buffer," native",-1);host.check();
    require(changes==1&&editor->editText()==u"BOLD native tail","Native text insertion did not emit a single Change");
    editor->editSelect(4,7);require(!editor->get(runtime,"selbold").truth()&&editor->get(runtime,"selfontsize").floating()==14,"Native typed text lost caret formatting");
    editor->set(runtime,"seltext",Value::string(u"\U0001f600"));require(editor->editText()==u"BOLD\U0001f600 tail"&&editor->editSelection().first==6,"SelText did not preserve UTF-16 caret indexing");
    editor->editSelect(0,4);require(editor->get(runtime,"selbold").truth(),"SelText flattened neighboring attributed runs");
    editor->set(runtime,"text",Value::string(u"first\nsecond\n"));editor->editSelect(1,2);editor->set(runtime,"selalignment",Value::integer(2));editor->set(runtime,"selindent",Value::integer(360));editor->set(runtime,"selhangingindent",Value::integer(180));editor->set(runtime,"selrightindent",Value::integer(90));
    editor->set(runtime,"seltabcount",Value::integer(2));editor->invoke(runtime,"let:seltabs",{Arg(Value::integer(0)),Arg(Value::integer(600))});
    require(editor->get(runtime,"selindent").integral()==360&&editor->get(runtime,"selhangingindent").integral()==180&&editor->invoke(runtime,"seltabs",{Arg(Value::integer(0))}).integral()==600,"Paragraph indents/tabs contract is wrong");
    editor->editSelect(0,1);require(editor->get(runtime,"selalignment").integral()==2,"Paragraph formatting failed to expand selection to the first character");
    editor->editSelect(6,1);require(editor->get(runtime,"selalignment").integral()==0,"Paragraph formatting escaped to the next paragraph");
    editor->set(runtime,"selbullet",Value::boolean(true));auto document=gtkRichSnapshot(*editor);same(document,gtkRtfParse(gtkRtfWrite(document)));
    host.formCall(form,"show",{});host.pump(); // Exercise the native bullet drawing path.
    editor->set(runtime,"textrtf",Value::string(u"{\\rtf1\\sl480\\slmult1\\tqr\\tx1440 A\\tab B\\par C}"));
    buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor->widget));gtk_text_buffer_get_start_iter(buffer,&iter);auto* aligned=gtk_text_iter_get_tags(&iter);require(aligned,"Native spacing tag missing");PangoTabArray* tabs=nullptr;g_object_get(aligned->data,"tabs",&tabs,nullptr);PangoTabAlign alignment;int position;pango_tab_array_get_tab(tabs,0,&alignment,&position);require(alignment==PANGO_TAB_RIGHT&&position==96,"RTF tab alignment did not reach Pango");pango_tab_array_free(tabs);
#if GTK_MAJOR_VERSION >= 4
    float lineHeight=0;g_object_get(aligned->data,"line-height",&lineHeight,nullptr);require(lineHeight>1.99f&&lineHeight<2.01f,"RTF double spacing did not reach GTK4 line-height");
#else
    int lineExtra=0;g_object_get(aligned->data,"pixels-inside-wrap",&lineExtra,nullptr);require(lineExtra>0,"RTF double spacing did not reach native wrapped-line geometry");
#endif
    g_slist_free(aligned);same(gtkRichSnapshot(*editor),gtkRtfParse(gtkRtfWrite(gtkRichSnapshot(*editor))));
    editor->set(runtime,"textrtf",Value::string(u"{\\rtf1 before {\\field{\\*\\fldinst HYPERLINK \"https://example.org\"}{\\fldrslt value}} tail}"));editor->editSelect(10,0);buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor->widget));gtk_text_buffer_insert_at_cursor(buffer,"-edit-",-1);host.check();
    auto changedField=gtkRichSnapshot(*editor);require(changedField.fields.size()==1&&changedField.fields[0].first==7&&changedField.fields[0].last==18&&changedField.fields[0].instruction==u"HYPERLINK \"https://example.org\"","Native editing did not track the field result marks");same(changedField,gtkRtfParse(gtkRtfWrite(changedField)));
    editor->editSelect(7,11);auto fieldFragment=editor->get(runtime,"selrtf");editor->set(runtime,"text",Value::string(u"paste: "));editor->editSelect(7,0);editor->set(runtime,"selrtf",fieldFragment);require(gtkRichSnapshot(*editor).fields.size()==1,"SelRTF lost a complete field wrapper");
    editor->set(runtime,"textrtf",Value::string(fromUTF8("{\\rtf1 before{\\pict\\pngblip\\picwgoal300\\pichgoal300 "+redPixel+"}after}")));buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor->widget));gtk_text_buffer_get_iter_at_offset(buffer,&iter,6);
#if GTK_MAJOR_VERSION < 4
    auto* pixels=gtk_text_iter_get_pixbuf(&iter);require(pixels&&gdk_pixbuf_get_width(pixels)==20&&gdk_pixbuf_get_height(pixels)==20&&gdk_pixbuf_get_pixels(pixels)[0]==255,"RTF picture did not create the expected native red image pixels");
#else
    auto* pixels=gtk_text_iter_get_paintable(&iter);require(GDK_IS_TEXTURE(pixels)&&gdk_texture_get_width(GDK_TEXTURE(pixels))==20&&gdk_texture_get_height(GDK_TEXTURE(pixels))==20,"RTF picture did not create the expected GTK4 texture");std::array<guchar,1600> rgba{};gdk_texture_download(GDK_TEXTURE(pixels),rgba.data(),80);require(rgba[2]==255&&rgba[3]==255,"RTF picture texture lost its red pixel content");
#endif
    require(editor->editText()==u"before\ufffcafter","Native embedded image lost its UTF-16 text anchor");editor->editSelect(6,1);auto pictureFragment=editor->get(runtime,"selrtf");editor->set(runtime,"selbold",Value::boolean(true));same(gtkRichSnapshot(*editor),gtkRtfParse(gtkRtfWrite(gtkRichSnapshot(*editor))));
    editor->set(runtime,"seltext",Value::string(u"x"));require(gtkRichSnapshot(*editor).objects.empty(),"Replacing an image did not release its object model");editor->editSelect(6,1);editor->set(runtime,"selrtf",pictureFragment);require(gtkRichSnapshot(*editor).objects.size()==1,"SelRTF did not restore an inline picture");
    const auto imageBefore=editor->get(runtime,"textrtf").string();auto damaged=gtkRtfParse(gtkRtfBytes(imageBefore));damaged.objects[0].bytes[damaged.objects[0].bytes.size()-18]^=1;error(380,[&]{editor->set(runtime,"textrtf",Value::string(fromUTF8(gtkRtfWrite(damaged))));});require(editor->get(runtime,"textrtf").string()==imageBefore,"Image decode failure changed the existing buffer");
    for(const auto* codec:{"jpeg","bmp"}){auto* source=gdk_pixbuf_new(GDK_COLORSPACE_RGB,FALSE,8,2,2);gdk_pixbuf_fill(source,0x208040ff);gchar* data=nullptr;gsize length=0;require(gdk_pixbuf_save_to_buffer(source,&data,&length,codec,nullptr,nullptr),"Cannot prepare native image codec fixture");g_object_unref(source);std::string bytes(data,length);g_free(data);if(std::string(codec)=="bmp")bytes.erase(0,14);editor->set(runtime,"textrtf",Value::string(fromUTF8("{\\rtf1{\\pict\\"+std::string(codec==std::string("bmp")?"dibitmap0":"jpegblip")+" "+hexBytes(bytes)+"}}")));auto snap=gtkRichSnapshot(*editor);require(snap.objects.size()==1&&snap.objects[0].bytes==bytes,"Native JPEG/DIB interchange lost original encoded bytes");same(snap,gtkRtfParse(gtkRtfWrite(snap)));}
    editor->set(runtime,"textrtf",Value::string(u"{\\rtf1 Before{\\trowd\\trgaph60\\clbrdrt\\brdrs\\cellx1440\\cellx2880\\b Left\\cell\\b0 Right\\cell\\row}After}"));buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor->widget));gtk_text_buffer_get_iter_at_offset(buffer,&iter,6);auto* anchor=gtk_text_iter_get_child_anchor(&iter);require(anchor,"Table row did not create a native child anchor");auto* grid=anchoredWidget(anchor);require(GTK_IS_GRID(grid),"RTF table was not rendered with native GtkGrid cells");auto* cell=richChild(gtk_grid_get_child_at(GTK_GRID(grid),0,0));require(GTK_IS_TEXT_VIEW(cell),"Table cell does not contain an editable native rich text view");changes=0;gtk_text_buffer_insert_at_cursor(gtk_text_view_get_buffer(GTK_TEXT_VIEW(cell)),"edited ",-1);host.check();auto editedTable=gtkRichSnapshot(*editor);require(editedTable.objects[0].cells[0].content->text.find(u"edited ")!=Text::npos&&changes==1&&editor->get(runtime,"modified").truth(),"Native table cell edits did not persist or dispatch one outer Change");same(editedTable,gtkRtfParse(gtkRtfWrite(editedTable)));
    editor->editSelect(7,1);auto tableBefore=editor->get(runtime,"textrtf").string();error(380,[&]{editor->set(runtime,"seltext",Value::string(u"partial"));});require(editor->get(runtime,"textrtf").string()==tableBefore,"Partial object replacement was not transactional");
    buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor->widget));GtkTextIter a,b;gtk_text_buffer_get_iter_at_offset(buffer,&a,7);gtk_text_buffer_get_iter_at_offset(buffer,&b,8);gtk_text_buffer_delete(buffer,&a,&b);host.check();require(editor->editText()==u"BeforeAfter"&&gtkRichSnapshot(*editor).objects.empty(),"Native deletion did not remove the complete atomic table object");
    editor->set(runtime,"textrtf",Value::string(u"{\\rtf1{\\header\\b Heading}body{\\footnote note}{\\object\\objemb{\\*\\objclass Test.Class}{\\*\\objdata 0102}{\\result cached}}}"));same(gtkRichSnapshot(*editor),gtkRtfParse(gtkRtfWrite(gtkRichSnapshot(*editor))));host.pump();
    editor->set(runtime,"text",Value::string(u"locked"));editor->editSelect(0,6);editor->set(runtime,"selprotected",Value::boolean(true));
    buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor->widget));gtk_text_buffer_get_iter_at_offset(buffer,&iter,2);require(!gtk_text_buffer_insert_interactive(buffer,&iter,"x",-1,TRUE),"SelProtected did not prevent native interactive edits");
    editor->set(runtime,"textrtf",preserved);auto before=editor->get(runtime,"textrtf").string();
    editor->invoke(runtime,"savefile",{Arg(Value::string(u"gtk-richtext-roundtrip.rtf"))});require(!editor->get(runtime,"modified").truth(),"SaveFile did not clear Modified");
    editor->set(runtime,"text",Value::string(u"changed"));editor->invoke(runtime,"loadfile",{Arg(Value::string(u"gtk-richtext-roundtrip.rtf"))});require(editor->get(runtime,"textrtf").string()==before,"Default LoadFile/SaveFile did not roundtrip RTF formatting");
    {std::ofstream file("gtk-richtext-invalid.rtf");file<<"{\\rtf1{\\object invalid}}";}
    auto priorSelection=editor->editSelection();error(380,[&]{editor->invoke(runtime,"loadfile",{Arg(Value::string(u"gtk-richtext-invalid.rtf"))});});
    require(editor->get(runtime,"textrtf").string()==before&&editor->editSelection()==priorSelection,"LoadFile failure replaced the previous document");
    editor->set(runtime,"text",Value::string(u"\u6f22\u5b57 \U0001f600"));editor->invoke(runtime,"savefile",{Arg(Value::string(u"gtk-richtext-utf8.txt")),Arg(Value::integer(1))});editor->set(runtime,"text",Value::string(u""));editor->invoke(runtime,"loadfile",{Arg(Value::string(u"gtk-richtext-utf8.txt")),Arg(Value::integer(1))});require(editor->editText()==u"\u6f22\u5b57 \U0001f600","Explicit plain UTF-8 file persistence lost Unicode");
    require(editor->invoke(runtime,"find",{Arg(Value::string(u"\U0001f600"))}).integral()==3,"Find returned a scalar offset instead of UTF-16 offset");
    require(selections>0,"RichTextBox SelChange did not dispatch");
    for(const char* name:{"gtk-richtext-roundtrip.rtf","gtk-richtext-invalid.rtf","gtk-richtext-utf8.txt"})std::filesystem::remove(name);
    runtime.unload(form);error(91,[&]{editor->get(runtime,"textrtf");});runtime.stop();
    std::cout<<"LINUX_GTK_RICHTEXT_OK\n";return 0;
  }catch(const Error& e){std::cerr<<"GTK RichTextBox VB error "<<e.number<<": "<<e.what()<<"\n";return 1;}
  catch(const std::exception& e){std::cerr<<"GTK RichTextBox contract failure: "<<e.what()<<"\n";return 1;}
}
