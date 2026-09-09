OUTPUT_FORMAT(binary)
SECTIONS
{
        /* Gost se ucitava i izvrsava od fizicke adrese 0x8000
           (memorija je identicki mapirana, VA == PA). */
        . = 0x8000;

        .start : { *(.start) }
        .text : { *(.text*) }
        .rodata : { *(.rodata*) }
        .data : { *(.data*) }
        .bss  : { *(.bss*) *(COMMON) }

        /* .note* sekcije (npr. .note.gnu.property) bi u binarnom izlazu
           zavrsile pre .start i pomerile ulaznu tacku, pa se odbacuju. */
        /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
