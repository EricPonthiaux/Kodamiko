import { User } from './user';
export function rename(user: User): string {
  user.name = 'x';
  return user.name;
}
