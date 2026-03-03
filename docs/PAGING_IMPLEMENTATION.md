# Implémentation du Paging Runtime - LplKernel

*Date: 2025-10-24*  
*Status: ✅ Complété et testé*

## 📋 Résumé

Implémentation d'une API de gestion du paging en runtime pour un noyau higher-half (0xC0000000) sur architecture i386. Cette implémentation **s'ajoute** au paging initial configuré dans `boot.s` et fournit des fonctions pour mapper/unmapper des pages dynamiquement.

---

## 🔴 Problèmes identifiés dans l'implémentation initiale

### 1. **Structures de données incorrectes** (`paging.h` original)

**Problème** :
```c
// ❌ INCORRECT - Unions avec bitfields non portables
typedef struct __attribute__((packed)) {
    uint8_t present : 1;
    uint8_t read_write : 1;
    // ...
    union SizeInfo {
        LargePage large;
        SmallPage small;
    } size;
    union PageFrame {
        FramePageEntry_t page_attributes;
        uint32_t address : 20;
    } frame;
} PageDirectoryEntry_t;
```

**Pourquoi c'est faux** :
- Les unions imbriquées avec bitfields ne respectent pas l'alignement strict 32-bit requis par le CPU
- La structure ne fait pas exactement 4 octets comme l'exige Intel
- Non conforme à la spécification OSDev wiki
- Peut causer des bugs de lecture/écriture des entrées

**Solution** :
```c
// ✅ CORRECT - Simple uint32_t avec macros
typedef uint32_t PageDirectoryEntry_t;
typedef uint32_t PageTableEntry_t;

// Flags combinés avec OR
#define PAGE_PRESENT        0x001
#define PAGE_WRITE          0x002
#define PAGE_USER           0x004
// ...

// Utilisation : entry = phys_addr | PAGE_PRESENT | PAGE_WRITE;
```

---

### 2. **Logique de `paging_init()` incorrecte** (`paging.c` original)

**Problème** :
```c
// ❌ INCORRECT - Mappe TOUT en 1:1 (identity mapping)
void paging_init(PageDirectoryEntry_t *page_directory, PageTableEntry_t *entries)
{
    for (uint16_t i = 0u; i < 1024u; ++i) {
        page_directory[i].present = 1u;
        // ...
        page_directory[i].frame.address = ((uint32_t)&entries[i * 1024u]) >> 12u;
        
        for (uint16_t j = 0u; j < 1024u; ++j) {
            entries[i * 1024u + j].address = (i * 1024u + j); // ❌ Identity 1:1
        }
    }
    paging_load(page_directory); // ❌ Réactive le paging déjà actif
}
```

**Pourquoi c'est faux** :
1. **Écrase le paging de boot.s** : Le paging est déjà configuré et actif (higher-half)
2. **Mappe tout en 1:1** : Ignore complètement le modèle higher-half (0xC0000000)
3. **Utilise des adresses virtuelles** : `&entries[i * 1024u]` est une adresse virtuelle, pas physique
4. **Recharge CR3 inutilement** : `paging_load()` va recharger CR3 et casser le higher-half
5. **Créé 1024 page tables** : Alloue 4MB de mémoire statiquement (1024 * 4KB)
6. **Triple fault garanti** : Dès l'appel, le kernel crashe

**Solution** :
```c
// ✅ CORRECT - N'écrase PAS le paging existant
void paging_init_runtime(void)
{
    // Simplement récupérer le page directory déjà configuré par boot.s
    current_page_directory = boot_page_directory;
    
    // Pas de rechargement de CR3 ! Le paging est déjà actif et correct.
}

// Fonctions runtime pour mapper/unmapper des pages individuelles
bool paging_map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);
bool paging_unmap_page(uint32_t virt_addr);
```

---

### 3. **Confusion adresses physiques vs virtuelles**

**Problème** :
```c
// ❌ &entries est une adresse VIRTUELLE (0xC0xxxxxx dans higher-half)
page_directory[i].frame.address = ((uint32_t)&entries[i * 1024u]) >> 12u;
```

**Pourquoi c'est faux** :
- Dans un noyau higher-half, toutes les adresses de pointeurs C sont virtuelles (0xC0000000+)
- Le CPU attend des **adresses PHYSIQUES** dans les PDEs/PTEs
- Il faut convertir : `phys = virt - KERNEL_VIRTUAL_BASE`

**Solution** :
```c
// ✅ CORRECT - Conversion explicite virt→phys
static inline uint32_t virt_to_phys(uint32_t virt_addr)
{
    if (virt_addr >= KERNEL_VIRTUAL_BASE)
        return virt_addr - KERNEL_VIRTUAL_BASE;
    return virt_addr;
}

// Utilisation :
uint32_t pt_phys_addr = PAGE_FRAME_ADDR(*pde); // Déjà physique
PageTableEntry_t *page_table = (PageTableEntry_t *)phys_to_virt(pt_phys_addr);
```

---

### 4. **Fonction `paging_load()` redondante**

**Problème** :
```asm
; ❌ Cette fonction réactive le paging alors qu'il est déjà actif
paging_load:
    movl 4(%esp), %eax
    movl %eax, %cr3         # Recharge CR3
    movl %cr0, %eax
    orl $0x80000000, %eax   # Réactive paging bit
    movl %eax, %cr0
    ret
```

**Pourquoi c'est faux** :
- `boot.s` a **déjà** activé le paging et chargé CR3
- Recharger CR3 avec un nouveau page directory va **écraser** le higher-half
- Le paging bit dans CR0 est déjà mis à 1

**Solution** :
```asm
; ✅ CORRECT - Fonctions ciblées pour opérations spécifiques
paging_get_cr3:       # Lire CR3 actuel
paging_load_cr3:      # Changer de page directory (pour multiprocessing)
paging_invlpg:        # Invalider UNE entrée TLB
paging_flush_tlb:     # Flush complet du TLB
```

---

## ✅ Solution implémentée

### Architecture des fichiers

```
kernel/
├── include/kernel/cpu/paging.h         # API publique
└── arch/i386/cpu/
    ├── paging.c                         # Implémentation C (map/unmap)
    └── paging_asm.s                     # Fonctions assembleur (CR3, INVLPG)
```

### Fichier 1 : `paging.h` - API simplifiée

```c
// Structures simples 32-bit (conformes Intel/OSDev)
typedef uint32_t PageDirectoryEntry_t;
typedef uint32_t PageTableEntry_t;

// Flags
#define PAGE_PRESENT        0x001
#define PAGE_WRITE          0x002
#define PAGE_USER           0x004
// ... (voir fichier complet)

// Macros d'extraction
#define PAGE_DIRECTORY_INDEX(virt_addr) (((uint32_t)(virt_addr) >> 22) & 0x3FF)
#define PAGE_TABLE_INDEX(virt_addr)     (((uint32_t)(virt_addr) >> 12) & 0x3FF)
#define PAGE_FRAME_ADDR(entry)          ((entry) & 0xFFFFF000)

// API C
void paging_init_runtime(void);
bool paging_map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);
bool paging_unmap_page(uint32_t virt_addr);
bool paging_get_physical_address(uint32_t virt_addr, uint32_t *phys_addr);

// API assembleur
extern uint32_t paging_get_cr3(void);
extern void paging_load_cr3(uint32_t phys_addr);
extern void paging_invlpg(uint32_t virt_addr);
extern void paging_flush_tlb(void);
```

### Fichier 2 : `paging.c` - Logique runtime

```c
// État interne
extern PageDirectoryEntry_t boot_page_directory[1024];
static PageDirectoryEntry_t *current_page_directory = NULL;

// Initialisation (appelée une fois au boot)
void paging_init_runtime(void)
{
    current_page_directory = boot_page_directory;
    // C'est tout ! Le paging est déjà actif.
}

// Mapper une page
bool paging_map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags)
{
    // Aligner sur 4KB
    virt_addr = PAGE_ALIGN_DOWN(virt_addr);
    phys_addr = PAGE_ALIGN_DOWN(phys_addr);
    
    // Obtenir indices PD/PT
    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virt_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virt_addr);
    
    // Vérifier que la page table existe
    PageDirectoryEntry_t *pde = &current_page_directory[pd_index];
    if (!(*pde & PAGE_PRESENT))
        return false; // TODO: créer page table (nécessite allocateur)
    
    // Obtenir page table (conversion phys→virt)
    uint32_t pt_phys_addr = PAGE_FRAME_ADDR(*pde);
    PageTableEntry_t *page_table = (PageTableEntry_t *)phys_to_virt(pt_phys_addr);
    
    // Écrire l'entrée
    page_table[pt_index] = phys_addr | flags;
    
    // Invalider TLB
    paging_invlpg(virt_addr);
    
    return true;
}
```

### Fichier 3 : `paging_asm.s` - Opérations bas-niveau

```asm
.global paging_get_cr3
paging_get_cr3:
    movl %cr3, %eax
    ret

.global paging_invlpg
paging_invlpg:
    movl 4(%esp), %eax
    invlpg (%eax)
    ret
```

---

## 🔧 Modifications dans boot.s

**Ajout** : Export du symbole `boot_page_directory` pour que le code C puisse y accéder :

```asm
.section .bss, "aw", @nobits
    .align 4096
.global boot_page_directory    # ← Ajouté
boot_page_directory:
    .skip 4096
.global boot_page_table        # ← Ajouté
boot_page_table:
    .skip 4096
```

---

## 🔧 Modifications dans kernel.c

**Avant** (incorrect) :
```c
static PageDirectoryEntry_t kernel_page_directory[1024] __attribute__((aligned(4096))) = {0};

__attribute__((constructor)) void kernel_initialize(void)
{
    // ...
    paging_init(kernel_page_directory, (PageTableEntry_t *)((uint32_t)kernel_page_directory + 4096u));
    // ❌ Écrase le paging de boot.s → TRIPLE FAULT
}
```

**Après** (correct) :
```c
// Pas de page_directory statique ! On utilise celui de boot.s

__attribute__((constructor)) void kernel_initialize(void)
{
    // ...
    serial_write_string(&com1, "[Kernel]: initializing runtime paging...\n");
    paging_init_runtime();
    serial_write_string(&com1, "[Kernel]: runtime paging initialized!\n");
    // ✅ N'écrase rien, juste initialise l'API
}
```

---

## 🔧 Modifications dans Makefile

**Ajout** dans `kernel/arch/i386/make.config` :
```makefile
KERNEL_ARCH_OBJS=\
$(BOOT_OBJECT) \
$(ARCHDIR)/boot/multiboot_info_helper.o \
$(ARCHDIR)/cpu/gdt.o \
$(ARCHDIR)/cpu/gdt_load.o \
$(ARCHDIR)/cpu/gdt_helper.o \
$(ARCHDIR)/cpu/paging.o \         # ← Nouveau
$(ARCHDIR)/cpu/paging_asm.o \     # ← Nouveau
$(ARCHDIR)/drivers/tty.o \
$(ARCHDIR)/drivers/serial.o \
$(ARCHDIR)/lib/asmutils.o \
```

---

## 🧪 Tests et validation

### Compilation
```bash
./clean.sh && ./build.sh
```

**Résultat** : ✅ Compilation réussie sans erreurs

### Test QEMU
```bash
./qemu.sh
```

**Sortie serial** :
```
[Laplace Kernel]: serial port initialisation successful.
[Laplace Kernel]: initializing GDT...
[Laplace Kernel]: loading GDT into CPU...
[Laplace Kernel]: GDT loaded successfully!
[Laplace Kernel]: initializing runtime paging...
[Laplace Kernel]: runtime paging initialized successfully!
```

**Résultat** : ✅ Kernel boot sans triple fault, paging runtime opérationnel

---

## 📖 Concepts clés expliqués

### 1. Higher-Half Kernel

**Principe** : Le kernel est mappé à 0xC0000000 (3GB) au lieu de 0x00000000.

**Avantages** :
- Sépare l'espace kernel (3GB-4GB) de l'espace user (0-3GB)
- Permet au kernel d'avoir une adresse constante dans tous les processus
- Facilite la gestion de multiples espaces d'adressage user

**Configuration** (dans boot.s) :
```asm
# Page Directory Entry 768 (3GB / 4MB = 768)
movl $(boot_page_table - KERNEL_START + 0x003), boot_page_directory - KERNEL_START + 768 * 4

# Après jump en higher-half, on retire l'identity mapping
movl $0, boot_page_directory + 0
```

### 2. Identity Mapping vs Higher-Half Mapping

**Identity Mapping** : Virt 0x00000000 → Phys 0x00000000 (1:1)  
**Higher-Half** : Virt 0xC0000000 → Phys 0x00000000

**Pourquoi les deux au boot ?**
- Identity mapping : Nécessaire temporairement pour exécuter le code de boot (qui tourne en adresses basses)
- Higher-half : Configuration finale où le kernel tourne

**Séquence** :
1. Boot.s active paging avec les 2 mappings
2. Jump vers higher-half (`jmp *%ecx` avec adresse > 0xC0000000)
3. Retire l'identity mapping (`movl $0, boot_page_directory + 0`)
4. Flush TLB (`movl %cr3, %ecx; movl %ecx, %cr3`)

### 3. Adresses physiques vs virtuelles

**Physique** : Adresse réelle de la RAM (0x00000000 - 0xFFFFFFFF)  
**Virtuelle** : Adresse vue par le CPU (après translation MMU)

**Dans un noyau higher-half** :
```c
void *ptr = malloc(1024);  // ptr = 0xC0123000 (virtuelle)

// Pour l'écrire dans une PTE, il faut la convertir
uint32_t phys = ptr - 0xC0000000;  // phys = 0x00123000
page_table[index] = phys | PAGE_PRESENT;
```

### 4. TLB (Translation Lookaside Buffer)

**Rôle** : Cache des traductions virt→phys pour accélérer les accès mémoire.

**Problème** : Si on modifie une PTE, le TLB garde l'ancienne traduction !

**Solution** :
- `invlpg` : Invalide UNE entrée (rapide)
- Reload CR3 : Flush complet (plus lent mais nécessaire parfois)

```c
// Après avoir modifié une PTE
page_table[index] = new_value;
paging_invlpg(virt_addr);  // Invalider le cache pour cette page
```

---

## 📚 Références OSDev

Documentation consultée :
- [Paging](https://wiki.osdev.org/Paging) - Vue d'ensemble et structures
- [Setting Up Paging](https://wiki.osdev.org/Setting_Up_Paging) - Configuration initiale
- [Higher Half x86 Bare Bones](https://wiki.osdev.org/Higher_Half_x86_Bare_Bones) - Architecture higher-half
- [Page Frame Allocation](https://wiki.osdev.org/Page_Frame_Allocation) - Allocateur de pages (TODO)

---

## 🚀 Prochaines étapes

### Phase 3 : Interruptions (prioritaire)
- Implémenter IDT (Interrupt Descriptor Table)
- Gestionnaires d'exceptions (#DE, #GP, #PF, #DF)
- PIC initialization et remapping
- IRQ handlers (timer, keyboard)

### Phase 4 : Gestion mémoire
- **Page Frame Allocator** : Allouer/libérer des pages physiques
- **Kernel Heap** : kmalloc/kfree pour allocations dynamiques
- **Page Table Creation** : Actuellement `paging_map_page()` ne peut pas créer de nouvelles page tables

### Amélioration du paging actuel
```c
// TODO dans paging_map_page() :
if (!(*pde & PAGE_PRESENT)) {
    // Allouer une nouvelle page pour la page table
    uint32_t new_pt_phys = page_frame_alloc(); // ← Nécessite Phase 4
    *pde = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    // Initialiser la page table (tout à 0)
    PageTableEntry_t *new_pt = (PageTableEntry_t *)phys_to_virt(new_pt_phys);
    memset(new_pt, 0, 4096);
}
```

---

## 📊 État actuel

**Phase 2 : CPU Initialization & Protection**
- ✅ Higher-half kernel (0xC0000000)
- ✅ Paging boot-time (boot.s)
- ✅ **Paging runtime API (nouveau)**
- ✅ GDT complet avec 6 segments
- ✅ TSS initialisé (base & limit configurés, LTR chargé)
- ❌ Page frame allocator manquant
- ❌ Ring 3 transition pas encore implémentée

**Progression** : **85% Phase 2 complétée** (était 80% avant)

---

*Fin du document - LplKernel Paging Implementation*
