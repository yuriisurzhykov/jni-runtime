package jnirt.test;

// A small value class: what a Convert<Frame> row of the collection tests reads and builds.
public final class Frame {
    public final int index;
    public final String label;

    public Frame(int index, String label) {
        this.index = index;
        this.label = label;
    }

    @Override
    public String toString() {
        return "Frame(" + index + ", " + label + ")";
    }
}
