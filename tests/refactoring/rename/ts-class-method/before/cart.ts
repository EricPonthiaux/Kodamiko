export class Cart {
  items: string[] = [];
  §add(item: string): void {
    this.items.push(item);
  }
}
