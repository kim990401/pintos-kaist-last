/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
/* 태현 추가 */
#include "threads/vaddr.h"
#include "threads/mmu.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void)
{
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	struct aux *aux = (struct aux *)page->uninit.aux;

	file_page->file = aux->file;
	file_page->ofs = aux->ofs;
	file_page->page_read_bytes = aux->page_read_bytes;
	file_page->page_zero_bytes = aux->page_zero_bytes;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy(struct page *page)
{
	struct file_page *file_page = &page->file;
	if (pml4_is_dirty(thread_current()->pml4, page->va))
	{
		file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}
	pml4_clear_page(thread_current()->pml4, page->va);
}

static bool
lazy_load_segment(struct page *page, void *aux)
{
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	/* 태현 추가 */
	struct aux *info = (struct aux *)aux;

	file_seek(info->file, info->ofs);

	if (file_read(info->file, page->frame->kva, info->page_read_bytes) != (int)info->page_read_bytes)
	{
		return false;
	}

	memset(page->frame->kva + info->page_read_bytes, 0, info->page_zero_bytes);

	free(aux);

	return true;
}

/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset)
{
	struct file *f = file_reopen(file);
	void *start_addr = addr; // 매핑 성공 시 파일이 매핑된 가상 주소 반환하는 데 사용
	// 이 매핑을 위해 사용한 총 페이지 수
	int cnt = length <= PGSIZE ? 1 : length % PGSIZE ? length / PGSIZE + 1
																  : length / PGSIZE;

	size_t read_bytes = file_length(f) < length ? file_length(f) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* 이 페이지를 채우는 방법을 계산합니다.
		파일에서 PAGE_READ_BYTES 바이트를 읽고
		최종 PAGE_ZERO_BYTES 바이트를 0으로 채웁니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct aux *aux = (struct aux *)malloc(sizeof(struct aux));
		aux->file = f;
		aux->ofs = offset;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;

		// vm_alloc_page_with_initializer를 호출하여 대기 중인 객체를 생성합니다.
		if (!vm_alloc_page_with_initializer(VM_FILE, addr,
											writable, lazy_load_segment, aux))
			return NULL;

		struct page *p = spt_find_page(&thread_current()->spt, start_addr);
		p->page_cnt = cnt;

		/* Advance. */
		// 읽은 바이트와 0으로 채운 바이트를 추적하고 가상 주소를 증가시킵니다.
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}

	return start_addr;
}

/* Do the munmap */
void do_munmap(void *addr)
{
	struct supplemental_page_table spt = thread_current()->spt;
	struct page *page = spt_find_page(&spt, addr);
	int cnt = page->page_cnt;
	while (cnt)
	{
		if (page)
		{
			destroy(page);
		}
		addr += PGSIZE;
		page = spt_find_page(&spt, addr);
		cnt--;
	}
}
