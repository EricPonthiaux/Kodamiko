import { User } from './user';

export function label(user: User): string {
  return user.fullName;
}
