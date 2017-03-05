!define StrStr "!insertmacro StrStr"

!macro StrStr ResultVar String SubString
  Push `${String}`
  Push `${SubString}`
  Call StrStr
  Pop `${ResultVar}`
!macroend

Function StrStr
  Exch $R0
  Exch
  Exch $R1
  Push $R2
  Push $R3
  Push $R4
  Push $R5

  StrLen $R2 $R0
  StrLen $R3 $R1
  StrCpy $R4 0

  loop:
    StrCpy $R5 $R1 $R2 $R4

    StrCmp $R5 $R0 done
    IntCmp $R4 $R3 done 0 done
    IntOp $R4 $R4 + 1
    Goto loop
  done:

  StrCpy $R0 $R1 `` $R4

  Pop $R5
  Pop $R4
  Pop $R3
  Pop $R2
  Pop $R1
  Exch $R0
FunctionEnd
