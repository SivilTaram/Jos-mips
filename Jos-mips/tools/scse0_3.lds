OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
  . = 0x80000080;
   .except_vec3 : {
  *(.text.exc_vec3)
    }
  . = 0x80010000;
  .text : {
        *(.text)
	*(.rodata)
	*(.rodata1)
	*(.rodata.str1.4)
        }

  .reginfo : { *(.reginfo) }

  .date : {
	*(.data)
	}

  .bss  : {
	*(.dynbss)
	*(.bss)
  }
   
  . = 0x803F8000;
  .kernelstk : {
	*(.data.stk)
	}

   . = 0x80400000;
   KVPT = . ;
.kernelvpt : {
  *(.data.vpt)
    }

   end = . ;
}
