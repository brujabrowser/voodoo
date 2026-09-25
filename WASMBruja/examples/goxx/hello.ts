// Canonical first example: TypeScript -> Go++. brujac emits `.goxx` so
// converted files are visible next to handwritten Go++ (`.go`).
interface Point {
  x: number;
  y: number;
}

function greet(name: string): string {
  return "hello " + name;
}

function origin(): Point {
  return { x: 0, y: 0 };
}

class Counter {
  value: number = 0;
  inc(): number {
    this.value = this.value + 1;
    return this.value;
  }
}

const p = origin();
const c = new Counter();
console.log(greet("bruja"));
console.log(p.x);
console.log(c.inc());
