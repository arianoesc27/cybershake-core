package data;

import java.io.DataInputStream;
import java.io.IOException;
import java.io.Serializable;

//AR: This mirrors RotDEntry.java, but for DirectSynth's own vertical response record, which
//AR: only has a period and a value, no rotation angle or RotD50/RotD100 fields, since the
//AR: vertical response is a single spectrum with no rotation involved.
public class VerticalRSPEntry implements Serializable {
	private static final long serialVersionUID = 1L;

	public float period;
	public float rsp;

	public VerticalRSPEntry() {
	}

	public int populate(DataInputStream stream) throws IOException {
		period = stream.readFloat();
		rsp = stream.readFloat();
		return 0;
	}

}
