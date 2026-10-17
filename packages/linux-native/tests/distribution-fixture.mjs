import {createControl,createForm} from '../../../src/project/model.js';

export const nativeDistributionProject={schema:1,name:'Native Package Żółw $(literal)',startup:'Sub Main',modules:[{name:'MainModule',kind:'module',code:`Option Explicit
Public Sub Main()
    Dim channel As Integer, value As String
    channel = FreeFile
    Open App.Path & "/../Resources/data.txt" For Input As #channel
    Line Input #channel, value
    Close #channel
    Debug.Assert value = "NATIVE_PACKAGE_RESOURCE_OK"
    Debug.Print "LINUX_NATIVE_DISTRIBUTION_OK"
End Sub`}]};

export const nativeDistributionInstanceProject={schema:1,name:'PackagedInstances',startup:'Sub Main',modules:[{name:'MainModule',kind:'module',code:`Option Explicit
Private Declare Sub Sleep Lib "kernel32" (ByVal milliseconds As Long)
Public Sub Main()
    If App.PrevInstance Then
        Debug.Print "INSTANCE_PREVIOUS"
    Else
        Debug.Print "INSTANCE_FIRST"
    End If
    Sleep 30000
End Sub`}]};

/** A real modal chooser loads language tables, then normal form ownership closes it. */
export function nativeDistributionFontProject() {
  const module=createForm('FontChooser','Packaged native font chooser');
  module.id='native-font-module';module.form.id='native-font-form';
  const dialog=createControl('CommonDialog','Dialog1',120,120),timer=createControl('Timer','Clock1',240,120);
  dialog.id='native-font-dialog';timer.id='native-font-timer';
  Object.assign(dialog.properties,{CancelError:0,Visible:0});Object.assign(timer.properties,{Enabled:0,Interval:0,Visible:0});
  module.form.controls.push(dialog,timer);
  module.code=`Option Explicit
Private Sub Form_Load()
    Dialog1.CancelError = False
    Dialog1.FontName = "DejaVu Sans"
    Dialog1.FontSize = 12
    Clock1.Interval = 1500
    Clock1.Enabled = True
    Dialog1.ShowFont
    Debug.Print "LINUX_GTK_FONT_CHOOSER_OK"
End Sub
Private Sub Clock1_Timer()
    Clock1.Enabled = False
    Unload Me
End Sub`;
  return {schema:1,id:'native-font-project',name:'NativeFontChooser',startup:module.name,modules:[module]};
}

export function nativeDistributionImageResources() {
  const ico=Buffer.alloc(70);ico.writeUInt16LE(1,2);ico.writeUInt16LE(1,4);ico[6]=1;ico[7]=1;
  ico.writeUInt16LE(1,10);ico.writeUInt16LE(32,12);ico.writeUInt32LE(48,14);ico.writeUInt32LE(22,18);
  ico.writeUInt32LE(40,22);ico.writeInt32LE(1,26);ico.writeInt32LE(2,30);ico.writeUInt16LE(1,34);ico.writeUInt16LE(32,36);
  ico.writeUInt32LE(4,42);ico[64]=255;ico[65]=255;
  return {'Resources/package-image.ico':ico,'Resources/package-image.svg':'<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8"><rect width="8" height="8" fill="red"/></svg>'};
}
