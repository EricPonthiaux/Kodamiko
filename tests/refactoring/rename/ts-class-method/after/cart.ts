export class Cart {
  items: string[] = [];
  insert(item: string): void {
    this.items.push(item);
  }
}
