public class PageTable{
	private final int ADDRESS_WIDTH = 32;
	private final int PAGE_SIZE = (int) Math.pow(2, 12); //Page Size = 4MB
	private PTE[] Table;

	public PageTable(){
		int numberOfEntries = (int) Math.pow(2, ADDRESS_WIDTH)/PAGE_SIZE; //Number of entries in the page table
		Table = new PTE[numberOfEntries];

		for(int i = 0; i < Table.length; i++){
			PTE newEntry = new PTE();
			Table[i] = newEntry;
		}
	}

	public PTE get(int index){
		return Table[index];
	}

	public int size(){
		return Table.length;
	}
}
