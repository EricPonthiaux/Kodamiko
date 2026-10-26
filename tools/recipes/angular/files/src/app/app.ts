import { Component, signal } from '@angular/core';

@Component({
  selector: 'app-root',
  templateUrl: './app.html',
  styleUrl: './app.css',
})
export class App {
  protected readonly name = signal('{{identifier}}');
  protected readonly clicks = signal(0);

  protected count(): void {
    this.clicks.update((clicks) => clicks + 1);
  }
}
