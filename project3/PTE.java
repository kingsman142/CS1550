public class PTE{
	private int index;
	private int frame;
	private boolean dirty;
	private boolean valid;
	private boolean referenced;

	public PTE(){
		index = -1;
		frame = -1;
		dirty = false;
		valid = false;
		referenced = false;
	}

	public boolean getDirty(){
		return dirty;
	}

	public boolean getReferenced(){
		return referenced;
	}

	public boolean getValid(){
		return valid;
	}

	public int getIndex(){
		return index;
	}

	public int getFrame(){
		return frame;
	}

	public void setDirty(boolean val){
		dirty = val;
	}

	public void setReferenced(boolean val){
		referenced = val;
	}

	public void setValid(boolean val){
		valid = val;
	}

	public void setIndex(int val){
		index = val;
	}

	public void setFrame(int val){
		frame = val;
	}
}
