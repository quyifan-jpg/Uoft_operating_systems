#include <stdio.h>
#include "list_2.h"


/* The following data structure uses the linux kernel linked list structure
 * as defined in list.h.  
 */
struct node {
	int data;
	struct list_entry my_entry;
};

int main() {

	// declare variable to point to head of the list
	list_head a_list;
	list_init(&a_list);
	
	struct node n1;
	n1.data = 10;
	list_entry_init(&n1.my_entry);

	struct node n2;
	n2.data = 20;
	list_entry_init(&n1.my_entry);

	list_add_tail(&a_list, &(n1.my_entry));
	list_add_head(&a_list, &(n2.my_entry));

	printf("Print list with one element\n");
	list_entry *current;


	// list_for_each(current, &a_list) {
	// 	struct node *b = container_of(current, struct node, my_entry);
	// 	printf("%d\n", b->data);
	// }
    for (current = a_list.head.next; current != &a_list.head; current = current->next){
        struct node *b = container_of(current, struct node, my_entry);
        printf("%d\n", b->data);
    }
}
